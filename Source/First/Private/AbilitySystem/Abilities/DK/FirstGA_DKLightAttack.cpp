#include "AbilitySystem/Abilities/DK/FirstGA_DKLightAttack.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputPress.h"
#include "Animation/AnimMontage.h"
#include "AbilitySystem/GameplayEffects/FirstGE_Damage.h"
#include "Character/DKCharacter.h"
#include "Character/BossCharacter.h"
#include "Components/Combat/DKCombatComponent.h"
#include "Components/Targeting/DKTargetLockComponent.h"
#include "Items/Weapons/DKWeapon.h"
#include "MyGameplayTags.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "Notifies/AnimNotifyState_ComboWindow.h"

// 作用：定义轻攻击在整个连击期间持有 Attacking Tag，并阻止闪避/死亡状态下启动。
UFirstGA_DKLightAttack::UFirstGA_DKLightAttack()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	// 只有被标记为可取消的 Ability，其他 Ability 才能通过 ASC 取消它。
	bIsCancelable = true;
	
	FGameplayTagContainer AssetTags;
	// 所有攻击的通用类别，供 Dodge / Parry / Skill 取消整个 Attack 家族。
	AssetTags.AddTag(MyGameplayTags::DK_Ability_Action);
	AssetTags.AddTag(MyGameplayTags::DK_Ability_Attack);
	AssetTags.AddTag(MyGameplayTags::DK_Ability_Attack_Light_Sword);
	SetAssetTags(AssetTags);

	ActivationOwnedTags.AddTag(MyGameplayTags::DK_Status_Attacking);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Dodging);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_ChangingWeapon);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Defending);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_GuardBroken);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Executing);
}

// 作用：开始一整次连击会话；后续按键进入本实例的 WaitInputPress，不会新建 Ability。
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
	ComboWindowClosedAt = -1.0;
	ActiveComboWindowSources.Reset();
	CurrentAttackMontage = nullptr;
	bTransitionToNextComboStep = false;

	if (ADKCharacter* DK = GetDKCharacterFromActorInfo())
	{
		ObservedTargetLock = DK->GetTargetLockComponent();
		if (UDKTargetLockComponent* TargetLock = ObservedTargetLock.Get())
		{
			TargetLock->OnTargetLockChanged.AddUniqueDynamic(this, &ThisClass::HandleTargetLockChanged);
		}
	}

	// 必须先监听，随后 Montage 的 Notify 才不会早于接收任务。
	StartEventListeners();
	StartCurrentComboStep();
}

void UFirstGA_DKLightAttack::HandleTargetLockChanged(AEnemyCharacter* NewTarget)
{
	// 解锁或切换时，当前这一刀停止向旧目标吸附。
	// 下一段连招重新读取锁定目标，避免出刀中途突然扭向另一个敌人。
	if (IsActive())
	{
		if (ADKCharacter* DK = GetDKCharacterFromActorInfo())
		{
			DK->EndAttackWarping();
		}
	}
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
	if (!IsActive())
	{
		return;
	}

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
	CombatComponent->ToggleWeaponCollision(false);
	
	bComboWindowOpen = false;
	// 新一段攻击开始时，窗口还没有过去，允许缓存输入。
	bComboWindowPassed = false;
	ComboWindowClosedAt = -1.0;
	ActiveComboWindowSources.Reset();
	bWantsNextCombo = false;
	bTransitionToNextComboStep = false;
	// 结束上一段遗留的输入任务：任务有明确生命周期，避免多个任务累积。
	if (ComboInputTask)
	{
		ComboInputTask->EndTask();
		ComboInputTask = nullptr;
	}
	
	// 保存本段正在播放的 Montage，后续 ComboWindow 结束时会停止它。
	CurrentAttackMontage = Weapon->DKWeaponData.LightAttackMontages[MontageIndex];

	// 每段开始都重建吸附会话。未锁定、目标死亡或切换目标时，基类会先清掉旧 AttackTarget。
	if (ADKCharacter* DK = GetDKCharacterFromActorInfo())
	{
		AEnemyCharacter* WarpTarget = DK->GetTargetLockComponent()
			? DK->GetTargetLockComponent()->GetCurrentTarget()
			: nullptr;
		DK->BeginAttackWarping(WarpTarget, AttackWarpingData);
	}

	// WaitInputPress 收到一次新按下便自动结束；一段只需缓存一次。
	// 先建立输入监听，播放失败时统一由 EndAbility 清理。
	ComboInputTask = UAbilityTask_WaitInputPress::WaitInputPress(this, false);
	ComboInputTask->OnPress.AddDynamic(this, &ThisClass::HandleComboInputPressed);
	ComboInputTask->ReadyForActivation();

	CurrentMontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, FName(*FString::Printf(TEXT("LightAttack_%d"), CurrentComboStep)),
		CurrentAttackMontage.Get(), 1.f, NAME_None, true, 1.f, 0.f, true);
	CurrentMontageTask->OnCompleted.AddDynamic(this, &ThisClass::HandleMontageCompleted);
	CurrentMontageTask->OnInterrupted.AddDynamic(this, &ThisClass::HandleMontageCancelled);
	CurrentMontageTask->OnCancelled.AddDynamic(this, &ThisClass::HandleMontageCancelled);
	CurrentMontageTask->ReadyForActivation();
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
	
	return Weapon->DKWeaponData.LightAttackMontages.IsValidIndex(CurrentComboStep) &&
		Weapon->DKWeaponData.LightAttackMontages[CurrentComboStep] != nullptr;
	
}

void UFirstGA_DKLightAttack::RequestNextComboStepTransition(bool bMontageCompleted)
{
	// 一个缓存只切一段；取消后的回调不能重新启动攻击。
	if (!IsActive() || !bWantsNextCombo || bTransitionToNextComboStep || !CanTransitionToNextComboStep())
	{
		return;
	}
	
	ADKCharacter* DkCharacter = GetDKCharacterFromActorInfo();
	UAnimInstance* AnimInstance = DkCharacter && DkCharacter->GetMesh() ? DkCharacter->GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance || !CurrentAttackMontage)
	{
		FinishAttack(true);
		return;
	}
	// 混出可能是自然结束，也可能是真实中断，而 UE 会先派发 Notify、
	// 后派发 montage 回调。此处保留缓存，等 Completed 兑现或 Interrupted
	// 取消，不能提前拆掉旧任务并吞掉尚未到达的真实中断。
	if (!bMontageCompleted && !AnimInstance->Montage_IsActive(CurrentAttackMontage.Get()))
	{
		return;
	}
	
	bTransitionToNextComboStep = true;
	bWantsNextCombo = false;
	ActiveComboWindowSources.Reset();

	// 自然混出后 Montage_IsPlaying 已为 false，Stop 也不保证再次发出
	// Interrupted。先拆除旧任务输出，再主动切段，避免已缓存的输入丢失。
	ClearCurrentMontageTask();
	AnimInstance->Montage_Stop(0.f, CurrentAttackMontage.Get());
	if (IsActive())
	{
		++CurrentComboStep;
		StartCurrentComboStep();
	}
}

void UFirstGA_DKLightAttack::ClearCurrentMontageTask()
{
	if (CurrentMontageTask)
	{
		CurrentMontageTask->OnCompleted.RemoveAll(this);
		CurrentMontageTask->OnInterrupted.RemoveAll(this);
		CurrentMontageTask->OnCancelled.RemoveAll(this);
		CurrentMontageTask->EndTask();
		CurrentMontageTask = nullptr;
	}
}

// 一次已经通过碰撞窗口去重的真实武器命中：
// 先结算生命伤害，再按武器数据请求削韧。
void UFirstGA_DKLightAttack::HandleMeleeHit(FGameplayEventData Payload)
{
	UDKCombatComponent* CombatComponent = GetDKCombatComponentFromActorInfo();
	AActor* TargetActor = const_cast<AActor*>(Payload.Target.Get());

	if (!CombatComponent || !TargetActor)
	{
		return;
	}

	const float WeaponBaseDamage = CombatComponent
		->GetDKCurrentEquippedWeaponDamageAtLevel(
			GetAbilityLevel());

	const float WeaponPoiseDamage = CombatComponent
		->GetDKCurrentEquippedWeaponPoiseDamageAtLevel(
			GetAbilityLevel());

	if (WeaponBaseDamage <= 0.f &&
		WeaponPoiseDamage <= 0.f)
	{
		return;
	}

	// 必须先结算生命伤害。
	// 如果这一击已经致死，随后 ApplyPoiseDamage 的 Dead 门控
	// 会阻止同一击再进入 Executable。
	if (WeaponBaseDamage > 0.f)
	{
		FGameplayEffectSpecHandle DamageSpec =
			MakeDKDamageEffectSpecHandle(
				UFirstGE_Damage::StaticClass(),
				WeaponBaseDamage,
				CurrentComboStep);

		// GameplayEventData 将 Target 暴露为 const AActor；ASC 查询 API 需要 AActor*。
		// const_cast 只用于取得目标 ASC，不会在这里直接修改目标 Actor 的普通成员。
		ApplyEffectSpecHandleToTarget(
			TargetActor,
			DamageSpec);
	}

	// 首版只让 ABossCharacter 消耗 Poise。
	// 普通敌人仍只接收上面的生命伤害。
	if (WeaponPoiseDamage > 0.f)
	{
		if (ABossCharacter* BossTarget =
			Cast<ABossCharacter>(TargetActor))
		{
			BossTarget->ApplyPoiseDamage(
				WeaponPoiseDamage,
				GetAvatarActorFromActorInfo());
		}
	}
}

// 同时核对动画与通知对象，避免旧动画的迟到通知影响新一刀。
void UFirstGA_DKLightAttack::HandleComboWindowOpened(FGameplayEventData Payload)
{
	if (!IsActive() || bTransitionToNextComboStep || bComboWindowPassed ||
		Payload.OptionalObject2 != CurrentAttackMontage ||
		!Cast<UAnimNotifyState_ComboWindow>(Payload.OptionalObject.Get()))
	{
		return;
	}
	
	ActiveComboWindowSources.Add(Payload.OptionalObject.Get());
	bComboWindowOpen = true;
	
	UE_LOG(LogTemp, Verbose, TEXT("[ComboTrace][GA] Window opened | Step=%d"), CurrentComboStep);
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

// 只关闭本段实际打开过的窗口；重复或旧段 Close 不会更改截止时间。
void UFirstGA_DKLightAttack::HandleComboWindowClosed(FGameplayEventData Payload)
{
	if (!IsActive() || bTransitionToNextComboStep || bComboWindowPassed ||
		Payload.OptionalObject2 != CurrentAttackMontage ||
		!ActiveComboWindowSources.Remove(Payload.OptionalObject.Get()) ||
		!ActiveComboWindowSources.IsEmpty())
	{
		return;
	}

	bComboWindowOpen = false;
	bComboWindowPassed = true;
	ComboWindowClosedAt = GetWorld()->GetTimeSeconds();
	
	UE_LOG(LogTemp, Verbose, TEXT("[ComboTrace][GA] Window closed | Step=%d | WantsNext=%d"), CurrentComboStep, bWantsNextCombo);
	
	// 玩家在窗口关闭前按过攻击，且动画正到达“后摇开始前”的窗口结束点。
	// 此时主动结束旧 Montage，跳过后摇。
	if (bWantsNextCombo)
	{
		RequestNextComboStepTransition();
	}
}

// 从本段起手开始缓存；关窗后短暂容错内的输入立即执行下一段。
void UFirstGA_DKLightAttack::HandleComboInputPressed(float TimeWaited)
{
	UE_LOG(LogTemp, Verbose, TEXT("[ComboTrace][GA] Input received | Step=%d | Passed=%d | WantsNext=%d | TimeWaited=%.3f"),
		CurrentComboStep, bComboWindowPassed, bWantsNextCombo, TimeWaited);
	
	if (!IsActive() || bWantsNextCombo || bTransitionToNextComboStep)
	{
		return;
	}
	if (bComboWindowPassed)
	{
		const double GracePeriod = FMath::IsFinite(ComboInputGracePeriod)
			? FMath::Max(0.f, ComboInputGracePeriod) : 0.f;
		if (GracePeriod <= 0.0 || ComboWindowClosedAt < 0.0 ||
			GetWorld()->GetTimeSeconds() - ComboWindowClosedAt > GracePeriod)
		{
			return;
		}
	}

	bWantsNextCombo = true;
	if (bComboWindowPassed)
	{
		RequestNextComboStepTransition();
	}
}

// 作用：本段正常结束后，若已缓存输入且还有 Montage，则进入下一段；否则结束连击。
void UFirstGA_DKLightAttack::HandleMontageCompleted()
{
	UDKCombatComponent* CombatComponent = GetDKCombatComponentFromActorInfo();
	ADKWeapon* Weapon = CombatComponent? CombatComponent->GetDKCurrentEquippedWeapon(): nullptr;

	const int32 MontageCount = Weapon? Weapon->DKWeaponData.LightAttackMontages.Num(): 0;
	UE_LOG(LogTemp, Verbose, TEXT("[ComboTrace][GA] Montage completed | Step=%d | WantsNext=%d | MontageCount=%d"),CurrentComboStep,bWantsNextCombo,MontageCount);
	// 自然完成时兜底兑现已接受的输入，覆盖通知因混出权重而漏发的情况。
	if (IsActive() && bWantsNextCombo && CanTransitionToNextComboStep())
	{
		RequestNextComboStepTransition(true);
		return;
	}
	
	FinishAttack(false);
}

// 作用：任何一段动画被打断都终止整次连击，避免从错误段数继续。
void UFirstGA_DKLightAttack::HandleMontageCancelled()
{
	// 主动切段已拆除旧任务回调；其余中断一律终止，不能兑现缓存。
	if (bTransitionToNextComboStep)
	{
		return;
	}
	UE_LOG(LogTemp, Verbose, TEXT("[ComboTrace][GA] Montage was interrupted or cancelled"));
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
	if (UDKTargetLockComponent* TargetLock = ObservedTargetLock.Get())
	{
		TargetLock->OnTargetLockChanged.RemoveDynamic(this, &ThisClass::HandleTargetLockChanged);
	}
	ObservedTargetLock.Reset();

	if (ComboInputTask)
	{
		ComboInputTask->EndTask();
		ComboInputTask = nullptr;
	}

	// 所有结束路径都销毁计时器和 AttackTarget，防止下一次攻击读取残留目标。
	if (ADKCharacter* DK = GetDKCharacterFromActorInfo())
	{
		DK->EndAttackWarping();
	}

	if (UDKCombatComponent* CombatComponent = GetDKCombatComponentFromActorInfo())
	{
		CombatComponent->ToggleWeaponCollision(false);
	}

	CurrentComboStep = 0;
	bComboWindowOpen = false;
	bWantsNextCombo = false;
	bComboWindowPassed = false;
	ComboWindowClosedAt = -1.0;
	ActiveComboWindowSources.Reset();
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
	// Super 让仍在运行的 montage task 按原规则停止动画，然后释放引用。
	CurrentMontageTask = nullptr;
}
