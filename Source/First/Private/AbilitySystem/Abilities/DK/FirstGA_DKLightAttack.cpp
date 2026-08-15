#include "AbilitySystem/Abilities/DK/FirstGA_DKLightAttack.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputPress.h"
#include "Animation/AnimMontage.h"
#include "AbilitySystem/GameplayEffects/FirstGE_Damage.h"
#include "Character/DKCharacter.h"
#include "Components/Combat/DKCombatComponent.h"
#include "Items/Weapons/DKWeapon.h"
#include "MyGameplayTags.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"

// 作用：定义轻攻击在整个连击期间持有 Attacking Tag，并阻止闪避/死亡状态下启动。
UFirstGA_DKLightAttack::UFirstGA_DKLightAttack()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	// 只有被标记为可取消的 Ability，其他 Ability 才能通过 ASC 取消它。
	bIsCancelable = true;
	
	FGameplayTagContainer AssetTags;
	// 所有攻击的通用类别，供 Dodge / Parry / Skill 取消整个 Attack 家族。
	AssetTags.AddTag(MyGameplayTags::DK_Ability_Attack);
	AssetTags.AddTag(MyGameplayTags::DK_Ability_Attack_Light_Sword);
	SetAssetTags(AssetTags);

	ActivationOwnedTags.AddTag(MyGameplayTags::DK_Status_Attacking);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Dodging);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_ChangingWeapon);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);
}

// 作用：开始一整次连击会话；后续两次按键只进入本实例的 WaitInputPress，不会新建 Ability。
void UFirstGA_DKLightAttack::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	UDKCombatComponent* CombatComponent = GetDKCombatComponentFromActorInfo();
	ADKWeapon* Weapon = CombatComponent? CombatComponent->GetDKCurrentEquippedWeapon(): nullptr;

	if (!Weapon || Weapon->DKWeaponData.LightAttackMontages.IsEmpty())
	{
		FinishAttack(true);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		FinishAttack(true);
		return;
	}

	CurrentComboStep = 1;
	bComboWindowOpen = false;
	bWantsNextCombo = false;
	bComboWindowPassed = false;
	CurrentAttackMontage = nullptr;
	bTransitionToNextComboStep = false;

	// 必须先监听，随后 Montage 的 Notify 才不会早于接收任务。
	StartEventListeners();
	StartCurrentComboStep();
}

// 作用：为本次 Ability 生命周期持续接收命中和 Combo Window 事件。
void UFirstGA_DKLightAttack::StartEventListeners()
{
	UAbilityTask_WaitGameplayEvent* HitTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::DK_Event_MeleeHit,
			nullptr,
			false,
			true);
	HitTask->EventReceived.AddDynamic(this, &ThisClass::HandleMeleeHit);
	HitTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* OpenTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::DK_Event_ComboWindow_Open,
			nullptr,
			false,
			true);
	OpenTask->EventReceived.AddDynamic(this, &ThisClass::HandleComboWindowOpened);
	OpenTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* CloseTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::DK_Event_ComboWindow_Close,
			nullptr,
			false,
			true);
	CloseTask->EventReceived.AddDynamic(this, &ThisClass::HandleComboWindowClosed);
	CloseTask->ReadyForActivation();
	
	UAbilityTask_WaitGameplayEvent* ActionCancelOpenTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, MyGameplayTags::DK_Event_ActionCancelWindow_Open, nullptr, false, true);
	
	ActionCancelOpenTask->EventReceived.AddDynamic(this, &ThisClass::HandleActionCancelWindowOpened);
	ActionCancelOpenTask->ReadyForActivation();
	
	UAbilityTask_WaitGameplayEvent* ActionCancelCloseTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, MyGameplayTags::DK_Event_ActionCancelWindow_Close, nullptr, false, true);
	ActionCancelCloseTask->EventReceived.AddDynamic(this, &ThisClass::HandleActionCancelWindowClosed);
	ActionCancelCloseTask->ReadyForActivation();
}

// 作用：根据 1-based 连击段数选择 0-based Montage 数组元素，并等待本段播放结果。
void UFirstGA_DKLightAttack::StartCurrentComboStep()
{
	UDKCombatComponent* CombatComponent = GetDKCombatComponentFromActorInfo();
	ADKWeapon* Weapon = CombatComponent? CombatComponent->GetDKCurrentEquippedWeapon(): nullptr;
	const int32 MontageIndex = CurrentComboStep - 1;

	if (!Weapon || !Weapon->DKWeaponData.LightAttackMontages.IsValidIndex(MontageIndex) ||
		!Weapon->DKWeaponData.LightAttackMontages[MontageIndex])
	{
		FinishAttack(true);
		return;
	}

	// Attack_1 → Attack_2 时，不允许上一段的取消窗口泄漏到下一段前摇。
	ClearActionCancelTags();
	
	bComboWindowOpen = false;
	// 新一段攻击开始时，窗口还没有过去，允许缓存输入。
	bComboWindowPassed = false;
	bWantsNextCombo = false;
	// 结束上一段遗留的输入任务：任务有明确生命周期，避免多个任务累积。
	if (ComboInputTask)
	{
		ComboInputTask->EndTask();
		ComboInputTask = nullptr;
	}
	
	// 保存本段正在播放的 Montage，后续 ComboWindow 结束时会停止它。
	CurrentAttackMontage = Weapon->DKWeaponData.LightAttackMontages[MontageIndex];
	
	UAbilityTask_PlayMontageAndWait* MontageTask =UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,FName(*FString::Printf(TEXT("LightAttack_%d"), CurrentComboStep)),CurrentAttackMontage.Get());

	MontageTask->OnCompleted.AddDynamic(this, &ThisClass::HandleMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::HandleMontageCancelled);
	MontageTask->OnCancelled.AddDynamic(this, &ThisClass::HandleMontageCancelled);
	MontageTask->ReadyForActivation();
	
	//本段一开始就创建输入监听，而不是等 ComboWindow 打开。
	// 这样起手阶段按下的攻击键也会被缓存，不再需要精确卡窗口。
	ComboInputTask = UAbilityTask_WaitInputPress::WaitInputPress(this, false);
	ComboInputTask->OnPress.AddDynamic(this, &ThisClass::HandleComboInputPressed);
	ComboInputTask->ReadyForActivation();
	
}

void UFirstGA_DKLightAttack::AddActionCancelTags(const FGameplayTagContainer& TagsToAdd)
{
	UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponentFromActorInfo();
	for (auto TagIt = TagsToAdd.CreateConstIterator(); TagIt; ++TagIt)
	{
		const FGameplayTag& Tag = *TagIt;
		int32& Count = ActionCancelTagCounts.FindOrAdd(Tag);
		
		// 第一次获得该权限时才真正写入 ASC。
		if (Count == 0 && ASC)
		{
			ASC->AddLooseGameplayTag(Tag);
		}
		++Count;
	}
}

void UFirstGA_DKLightAttack::RemoveActionCancelTags(const FGameplayTagContainer& TagsToRemove)
{
	UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponentFromActorInfo();
	
	for (auto TagIt = TagsToRemove.CreateConstIterator(); TagIt; ++TagIt)
	{
		const FGameplayTag& Tag = *TagIt;
		int32* Count = ActionCancelTagCounts.Find(Tag);
		if (!Count)
		{
			continue;
		}
		
		--(*Count);
		
		// 只有最后一个同类窗口关闭时，才真的移除ASC标签
		if (*Count <= 0)
		{
			if (ASC)
			{
				ASC->RemoveLooseGameplayTag(Tag);
			}
			
			ActionCancelTagCounts.Remove(Tag);
		}
		
	}
	
}

void UFirstGA_DKLightAttack::ClearActionCancelTags()
{
	UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponentFromActorInfo();
	
	// 无论 Notify 是否正常收到 End，攻击结束时都保证归还所有权限。
	if (ASC)
	{
		for (const TPair<FGameplayTag, int32>& Pair: ActionCancelTagCounts)
		{
			ASC->RemoveLooseGameplayTag(Pair.Key);
		}
	}
	
	ActionCancelTagCounts.Empty();
}

bool UFirstGA_DKLightAttack::CanTransitionToNextComboStep()
{
	UDKCombatComponent* CombatComponent = GetDKCombatComponentFromActorInfo();
	ADKWeapon* Weapon = CombatComponent? CombatComponent->GetDKCurrentEquippedWeapon(): nullptr;
	
	if (!Weapon)
	{
		return false;
	}
	
	return Weapon->DKWeaponData.LightAttackMontages.IsValidIndex(CurrentComboStep);
	
}

void UFirstGA_DKLightAttack::RequestNextComboStepTransition()
{
	// 防止重复请求、窗口外请求，以及最后一段攻击继续尝试连招。
	if (!IsActive() || !bWantsNextCombo || bTransitionToNextComboStep || !CanTransitionToNextComboStep())
	{
		return;
	}
	
	ADKCharacter* DkCharacter = GetDKCharacterFromActorInfo();
	UAnimInstance* AnimInstance = DkCharacter && DkCharacter->GetMesh() ? DkCharacter->GetMesh()->GetAnimInstance() : nullptr;
	// 当前动画已结束或动画实例不存在时，不强行处理；
	// 正常的 Montage Completed 回调会负责结束本次 Ability。
	if (!AnimInstance || !CurrentAttackMontage || !AnimInstance->Montage_IsPlaying(CurrentAttackMontage.Get()))
	{
		return;
	}
	
	// 从这里开始的 Interrupted 是“预期中的连招切换”，不是异常取消。
	bTransitionToNextComboStep = true; 
	
	// 0.10 秒是初始推荐值：
	// 太小会显得突兀；太大则会有拖沓感。
	constexpr float ComboTransitionBlendOutTime = 0.0f;
	
	// 停止旧攻击。旧 Montage 的任务会收到 OnInterrupted，
	// 随后在 HandleMontageCancelled 中启动下一段攻击。
	AnimInstance->Montage_Stop(ComboTransitionBlendOutTime, CurrentAttackMontage.Get());
}

// 作用：把一次去重后的武器命中转换为带 BaseDamage/ComboCount 的 Damage Spec。
void UFirstGA_DKLightAttack::HandleMeleeHit(FGameplayEventData Payload)
{
	UDKCombatComponent* CombatComponent = GetDKCombatComponentFromActorInfo();
	const AActor* ConstTargetActor = Payload.Target.Get();
	if (!CombatComponent || !ConstTargetActor)
	{
		return;
	}

	const float WeaponBaseDamage =
		CombatComponent->GetDKCurrentEquippedWeaponDamageAtLevel(GetAbilityLevel());
	if (WeaponBaseDamage <= 0.f)
	{
		return;
	}

	FGameplayEffectSpecHandle DamageSpec = MakeDKDamageEffectSpecHandle(
		UFirstGE_Damage::StaticClass(),
		WeaponBaseDamage,
		CurrentComboStep);

	// GameplayEventData 将 Target 暴露为 const AActor；ASC 查询 API 需要 AActor*。
	// const_cast 只用于取得目标 ASC，不会在这里直接修改目标 Actor 的普通成员。
	ApplyEffectSpecHandleToTarget(
		const_cast<AActor*>(ConstTargetActor),
		DamageSpec);
}

// 作用：动画进入可接招区间时，才创建一次 WaitInputPress。
void UFirstGA_DKLightAttack::HandleComboWindowOpened(FGameplayEventData Payload)
{
	if (!IsActive())
	{
		return;
	}
	
	bComboWindowOpen = true;
	
	UE_LOG(LogTemp,Warning,TEXT("[ComboTrace][GA] Window opened | IsActive=%d | WasOpen=%d"),IsActive(),bComboWindowOpen);
}

void UFirstGA_DKLightAttack::HandleActionCancelWindowOpened(FGameplayEventData Payload)
{
	if (!IsActive())
	{
		return;
	}
	
	AddActionCancelTags(Payload.InstigatorTags);
}

void UFirstGA_DKLightAttack::HandleActionCancelWindowClosed(FGameplayEventData Payload)
{
	RemoveActionCancelTags(Payload.InstigatorTags);
}

// 作用：动画离开接招区间时取消尚未触发的输入任务，严格拒绝窗口外输入。
void UFirstGA_DKLightAttack::HandleComboWindowClosed(FGameplayEventData Payload)
{
	bComboWindowOpen = false;
	// 窗口已经结束：之后的输入不再进入缓存。
	bComboWindowPassed = true;
	
	UE_LOG(LogTemp,Warning,TEXT("[ComboTrace][GA] Window closed | WantsNext=%d"),bWantsNextCombo);
	
	// 玩家在窗口关闭前按过攻击，且动画正到达“后摇开始前”的窗口结束点。
	// 此时主动结束旧 Montage，跳过后摇。
	if (bWantsNextCombo)
	{
		RequestNextComboStepTransition();
	}
}

// 作用：只记录玩家在有效窗口内确实再次按下；是否跳到下一段等当前 Montage 完成再决定。
void UFirstGA_DKLightAttack::HandleComboInputPressed(float TimeWaited)
{
	UE_LOG(LogTemp,Warning,TEXT("[ComboTrace][GA] Input received | Passed=%d | WantsNext=%d | TimeWaited=%.3f"),
		bComboWindowPassed,bWantsNextCombo,TimeWaited);
	
	// 已经缓存过一次，或窗口已经关闭：后续输入直接忽略。
	if (bWantsNextCombo || bComboWindowPassed)
	{
		return;
	}

	bWantsNextCombo = true;
	// 注意：这里不再把 ComboInputTask 置空，也不再结束它。
	// 任务继续存活，用于接收“窗口关闭前”的输入；
	// 清理统一交给 StartCurrentComboStep 和 EndAbility。
}

// 作用：本段正常结束后，若已缓存输入且还有 Montage，则进入下一段；否则结束连击。
void UFirstGA_DKLightAttack::HandleMontageCompleted()
{
	UDKCombatComponent* CombatComponent = GetDKCombatComponentFromActorInfo();
	ADKWeapon* Weapon = CombatComponent? CombatComponent->GetDKCurrentEquippedWeapon(): nullptr;

	const int32 MontageCount = Weapon? Weapon->DKWeaponData.LightAttackMontages.Num(): 0;
	UE_LOG(LogTemp,Warning,TEXT("[ComboTrace][GA] Montage completed | Step=%d | WantsNext=%d | MontageCount=%d"),CurrentComboStep,bWantsNextCombo,MontageCount);
	
	FinishAttack(false);
}

// 作用：任何一段动画被打断都终止整次连击，避免从错误段数继续。
void UFirstGA_DKLightAttack::HandleMontageCancelled()
{
	// 如果是我们在 ComboWindow 结束点主动停止的 Montage，
	// 这不是异常中断，而是进入下一段攻击的信号。
	if (bTransitionToNextComboStep)
	{
		bTransitionToNextComboStep = false;

		// 此时从 Attack_1 变为 Attack_2，依此类推。
		++CurrentComboStep;
		StartCurrentComboStep();
		return;
	}
	UE_LOG(LogTemp,Warning,TEXT("[ComboTrace][GA] Montage was interrupted or cancelled"));
	FinishAttack(true);
}

// 作用：只结束一次当前攻击 Ability。
void UFirstGA_DKLightAttack::FinishAttack(bool bWasCancelled)
{
	if (!IsActive())
	{
		return;
	}

	EndAbility(
		CurrentSpecHandle,
		CurrentActorInfo,
		CurrentActivationInfo,
		true,
		bWasCancelled);
}

// 作用：为所有结束路径提供最后一道清理，尤其防止动画被打断时武器碰撞保持开启。
void UFirstGA_DKLightAttack::EndAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,bool bReplicateEndAbility,bool bWasCancelled)
{
	if (ComboInputTask)
	{
		ComboInputTask->EndTask();
		ComboInputTask = nullptr;
	}

	if (UDKCombatComponent* CombatComponent = GetDKCombatComponentFromActorInfo())
	{
		CombatComponent->ToggleWeaponCollision(false);
	}

	CurrentComboStep = 0;
	bComboWindowOpen = false;
	bWantsNextCombo = false;
	bComboWindowPassed = false;
	CurrentAttackMontage = nullptr;
	bTransitionToNextComboStep = false;
	
	
	
	// 攻击正常结束、被 Dodge 取消、死亡打断时都会走到这里。
	ClearActionCancelTags();
	Super::EndAbility(
		Handle,
		ActorInfo,
		ActivationInfo,
		bReplicateEndAbility,
		bWasCancelled);
}