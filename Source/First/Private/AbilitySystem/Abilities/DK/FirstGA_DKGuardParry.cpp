#include "AbilitySystem/Abilities/DK/FirstGA_DKGuardParry.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "AbilitySystem/FirstAttributeSet.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/DKCharacter.h"
#include "Components/Combat/DKCombatComponent.h"
#include "Components/Combat/DKDefenseComponent.h"
#include "Components/Targeting/DKTargetLockComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MyGameplayTags.h"

UFirstGA_DKGuardParry::UFirstGA_DKGuardParry()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	bIsCancelable = true;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::DK_Ability_Action);
	AssetTags.AddTag(MyGameplayTags::DK_Ability_GuardParry);
	SetAssetTags(AssetTags);

	// Defending 覆盖 Start、Loop、Hit、Parry 和 End 整个生命周期。
	// 真正抵消伤害的 Blocking 由本类用 Loose Tag 单独控制。
	ActivationOwnedTags.AddTag(MyGameplayTags::DK_Status_Defending);

	// 表达“当前 Guard 可以被 Dodge 取消”，覆盖整个 Ability 生命周期，
	// 与攻击侧由动画窗口授予 .By.Dodge 的方式相互独立。
	ActivationOwnedTags.AddTag(MyGameplayTags::DK_Status_Action_Cancelable_By_Dodge);

	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Dodging);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_ChangingWeapon);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Defending);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_GuardBroken);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Executing);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);

	// 不要把 DK.Status.Attacking 加到 ActivationBlockedTags。
	// 攻击中能否转格挡，要在 CanActivateAbility 中读取 .By.Parry 窗口。
}

bool UFirstGA_DKGuardParry::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(
		Handle,
		ActorInfo,
		SourceTags,
		TargetTags,
		OptionalRelevantTags))
	{
		return false;
	}

	const ADKCharacter* DK = ActorInfo? Cast<ADKCharacter>(ActorInfo->AvatarActor.Get()): nullptr;
	const UAbilitySystemComponent* ASC = ActorInfo? ActorInfo->AbilitySystemComponent.Get(): nullptr;

	if (!DK || !ASC || !DK->GetDKDefenseComponent())
	{
		return false;
	}

	const UCharacterMovementComponent* Movement =DK->GetCharacterMovement();
	if (!Movement || !Movement->IsMovingOnGround())
	{
		return false;
	}

	// 首版是“持剑格挡”，未装备剑时不能启动。
	const UDKCombatComponent* Combat = DK->GetDKCombatComponent();
	if (!Combat ||Combat->CurrentEquippedWeaponTag != MyGameplayTags::DK_Weapon_Sword)
	{
		return false;
	}

	const float CurrentStamina = ASC->GetNumericAttribute(UFirstAttributeSet::GetStaminaAttribute());
	if (CurrentStamina <DK->GetDKDefenseComponent()->GetMinimumStaminaToGuard())
	{
		return false;
	}

	const bool bIsAttacking = ASC->HasMatchingGameplayTag(MyGameplayTags::DK_Status_Attacking);

	if (!bIsAttacking)
	{
		return true;
	}

	// 攻击中只有动画明确开放 By.Parry 取消窗口时才允许举剑。
	return ASC->HasMatchingGameplayTag(MyGameplayTags::DK_Status_Action_Cancelable_By_Parry);
}

void UFirstGA_DKGuardParry::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ADKCharacter* DK = GetDKCharacterFromActorInfo();
	UFirstAbilitySystemComponent* ASC =GetFirstAbilitySystemComponentFromActorInfo();
	UDKDefenseComponent* Defense =DK ? DK->GetDKDefenseComponent() : nullptr;

	if (!DK || !ASC || !Defense)
	{
		FinishGuard(true);
		return;
	}

	// 虽然当前没有 Cost/Cooldown，仍保留标准提交点，方便以后扩展。
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		FinishGuard(true);
		return;
	}

	// 清掉同一帧可能已由原生 Jump() 设置、但移动组件尚未消费的 pending jump。
	DK->StopJumping();

	// 只有 Guard 已成功提交后才取消当前攻击，避免激活失败却中断攻击。
	CancelActiveAttackAbilities();

	bExitRequested = false;
	bParryRiposteActive = false;
	bTargetLockWasActiveAtStart =DK->GetTargetLockComponent() &&DK->GetTargetLockComponent()->IsTargetLocked();

	ASC->AddLooseGameplayTag(MyGameplayTags::DK_Status_Blocking);
	bOwnsBlockingTag = true;

	ASC->AddLooseGameplayTag(MyGameplayTags::DK_Status_ParryWindow);
	bOwnsParryWindowTag = true;

	if (UCharacterMovementComponent* Movement = DK->GetCharacterMovement())
	{
		bSavedOrientRotationToMovement = Movement->bOrientRotationToMovement;
		bSavedUseControllerDesiredRotation =Movement->bUseControllerDesiredRotation;
		bSavedMovementState = true;

		Movement->StopMovementImmediately();
		Movement->MaxWalkSpeed = Defense->GetGuardMoveSpeed();
		Movement->bOrientRotationToMovement = false;
		Movement->bUseControllerDesiredRotation = true;
	}

	// 监听输入松开。true 表示如果松开边沿比任务建立更早，也立即回调。
	UAbilityTask_WaitInputRelease* ReleaseTask =UAbilityTask_WaitInputRelease::WaitInputRelease(this, true);
	ReleaseTask->OnRelease.AddDynamic(this,&ThisClass::HandleInputReleased);
	ReleaseTask->ReadyForActivation();

	// 弹反窗口只存在前 0.15 秒；Blocking 不受这个 Delay 影响。
	UAbilityTask_WaitDelay* ParryWindowTask =
		UAbilityTask_WaitDelay::WaitDelay(this,Defense->GetParryWindowDuration());
	ParryWindowTask->OnFinish.AddDynamic(this,&ThisClass::HandleParryWindowElapsed);
	ParryWindowTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* GuardHitTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::DK_Event_GuardHit,
			nullptr,
			false,
			true);
	GuardHitTask->EventReceived.AddDynamic(this,&ThisClass::HandleGuardHit);
	GuardHitTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* ParrySuccessTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::DK_Event_ParrySuccess,
			nullptr,
			false,
			true);
	ParrySuccessTask->EventReceived.AddDynamic(this,&ThisClass::HandleParrySuccess);
	ParrySuccessTask->ReadyForActivation();

	// 连锁弹反请求：角色层只在 ParryChainWindow 标签存在时把弹反按键转成本事件。
	UAbilityTask_WaitGameplayEvent* ParryChainTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::DK_Event_ParryChainRequest,
			nullptr,
			false,
			true);
	ParryChainTask->EventReceived.AddDynamic(this,&ThisClass::HandleParryChainPressed);
	ParryChainTask->ReadyForActivation();

	ActiveGuardMontage = DK->GetGuardParryMontage();
	if (!ActiveGuardMontage)
	{
		// 没有表现资源时仍允许测试标签和数值；松开后正常结束。
		return;
	}

	UAbilityTask_PlayMontageAndWait* MontageTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			TEXT("GuardParryMontage"),
			ActiveGuardMontage,
			1.f,
			TEXT("Start"));

	MontageTask->OnCompleted.AddDynamic(this,&ThisClass::HandleMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(this,&ThisClass::HandleMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this,&ThisClass::HandleMontageInterrupted);
	MontageTask->ReadyForActivation();
}

void UFirstGA_DKGuardParry::HandleParryWindowElapsed()
{
	RemoveParryWindowTag();
}

void UFirstGA_DKGuardParry::HandleInputReleased(float TimeHeld)
{
	BeginGuardExit();
}

void UFirstGA_DKGuardParry::HandleGuardHit(FGameplayEventData Payload)
{
	// 反击演出期间不再跳 Hit 段：格挡数值照常结算，表现保持 Parry 段完整。
	if (!bExitRequested && !bParryRiposteActive)
	{
		JumpToGuardSection(TEXT("Hit"));
	}
}

void UFirstGA_DKGuardParry::HandleParrySuccess(FGameplayEventData Payload)
{
	if (!bExitRequested)
	{
		// 进入反击演出：从这一刻起松键不再截断动画（方案 A）。
		bParryRiposteActive = true;
		JumpToGuardSection(TEXT("Parry"));
	}
}

void UFirstGA_DKGuardParry::HandleParryChainPressed(FGameplayEventData Payload)
{
	// "按了才连"：本事件只由按键边沿产生；退出流程中不连锁。
	if (bExitRequested || !IsActive())
	{
		return;
	}

	// 跳回 Start 段重接格挡表现并复位反击状态：
	// 此后格挡命中/弹反成功/松键各路径都走原有逻辑。
	// 播放头离开 Parry 段会触发 ANS_ParryChainWindow::NotifyEnd 摘掉窗口标签，
	// 一次窗口通过只连锁一次。
	bParryRiposteActive = false;
	JumpToGuardSection(TEXT("Start"));

	ReopenParryWindow();

	// 重臂松开监听：原来的 WaitInputRelease 已消费过，不重臂会导致
	// 连锁后的这轮格挡无法通过松键退出。
	if (UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponentFromActorInfo())
	{
		if (const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(CurrentSpecHandle))
		{
			// 连锁触发点就是按下边沿，此刻按键通常仍按住：等真实松开边沿。
			// 极短点按已松开的极少数情况传 true，让任务立即补发松开回调，
			// 本轮连锁按"点按"语义正常进入退出流程。
			const bool bAlreadyReleased = !Spec->InputPressed;

			UAbilityTask_WaitInputRelease* ReleaseTask =
				UAbilityTask_WaitInputRelease::WaitInputRelease(this, bAlreadyReleased);
			ReleaseTask->OnRelease.AddDynamic(this,&ThisClass::HandleInputReleased);
			ReleaseTask->ReadyForActivation();
		}
	}
}

void UFirstGA_DKGuardParry::ReopenParryWindow()
{
	UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponentFromActorInfo();
	ADKCharacter* DK = GetDKCharacterFromActorInfo();
	UDKDefenseComponent* Defense = DK ? DK->GetDKDefenseComponent() : nullptr;
	if (!ASC || !Defense)
	{
		return;
	}

	// 重新挂弹反资格并重启窗口计时；时长沿用格挡组件的 ParryWindowDuration，
	// 与首次弹反窗口完全同源。RemoveParryWindowTag 按所有权标志清理，可安全复用。
	ASC->AddLooseGameplayTag(MyGameplayTags::DK_Status_ParryWindow);
	bOwnsParryWindowTag = true;

	UAbilityTask_WaitDelay* ParryWindowTask =
		UAbilityTask_WaitDelay::WaitDelay(this, Defense->GetParryWindowDuration());
	ParryWindowTask->OnFinish.AddDynamic(this,&ThisClass::HandleParryWindowElapsed);
	ParryWindowTask->ReadyForActivation();
}

void UFirstGA_DKGuardParry::HandleMontageCompleted()
{
	FinishGuard(false);
}

void UFirstGA_DKGuardParry::HandleMontageInterrupted()
{
	FinishGuard(true);
}

void UFirstGA_DKGuardParry::CancelActiveAttackAbilities()
{
	UFirstAbilitySystemComponent* ASC =GetFirstAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}

	FGameplayTagContainer AttackTags;
	AttackTags.AddTag(MyGameplayTags::DK_Ability_Attack);
	ASC->CancelAbilities(&AttackTags, nullptr, this);
}

void UFirstGA_DKGuardParry::RemoveBlockingTag()
{
	if (!bOwnsBlockingTag)
	{
		return;
	}

	if (UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(MyGameplayTags::DK_Status_Blocking);
	}

	bOwnsBlockingTag = false;
}

void UFirstGA_DKGuardParry::RemoveParryWindowTag()
{
	if (!bOwnsParryWindowTag)
	{
		return;
	}

	if (UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(MyGameplayTags::DK_Status_ParryWindow);
	}

	bOwnsParryWindowTag = false;
}

void UFirstGA_DKGuardParry::JumpToGuardSection(FName SectionName)
{
	ADKCharacter* DK = GetDKCharacterFromActorInfo();
	UAnimInstance* AnimInstance =DK && DK->GetMesh() ? DK->GetMesh()->GetAnimInstance() : nullptr;

	if (!AnimInstance || !ActiveGuardMontage ||ActiveGuardMontage->GetSectionIndex(SectionName) == INDEX_NONE)
	{
		return;
	}

	AnimInstance->Montage_JumpToSection(SectionName, ActiveGuardMontage);
}

void UFirstGA_DKGuardParry::BeginGuardExit()
{
	if (bExitRequested)
	{
		return;
	}

	bExitRequested = true;

	// 松开第一帧就失去防御资格；End Section 只是表现。
	RemoveParryWindowTag();
	RemoveBlockingTag();

	// 反击演出进行中：不跳 End 段截断动画，Parry 段完整播完后
	// 由 HandleMontageCompleted 收势结束整个格挡。
	if (bParryRiposteActive)
	{
		// 没有蒙太奇资产时（纯数值测试模式）没有 OnCompleted 可依赖，直接结束。
		if (!ActiveGuardMontage)
		{
			FinishGuard(false);
		}
		return;
	}

	if (!ActiveGuardMontage ||ActiveGuardMontage->GetSectionIndex(TEXT("End")) == INDEX_NONE)
	{
		FinishGuard(false);
		return;
	}

	JumpToGuardSection(TEXT("End"));
}

void UFirstGA_DKGuardParry::FinishGuard(bool bWasCancelled)
{
	if (!IsActive())
	{
		return;
	}

	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,bWasCancelled);
}

void UFirstGA_DKGuardParry::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	RemoveParryWindowTag();
	RemoveBlockingTag();

	if (ADKCharacter* DK = GetDKCharacterFromActorInfo())
	{
		if (UCharacterMovementComponent* Movement = DK->GetCharacterMovement();
			bSavedMovementState && Movement)
		{
			// 不恢复进入 Guard 时的旧快照。Shift 可能在 Guard 期间按下或松开，
			// 应根据 ADKCharacter 当前保存的跑步状态重新决定 250/500。
			Movement->MaxWalkSpeed = DK->GetDesiredLocomotionSpeed();

			const bool bTargetLockIsActiveNow =DK->GetTargetLockComponent() &&DK->GetTargetLockComponent()->IsTargetLocked();

			// 锁定模式若在 Guard 期间因目标死亡而自动变化，
			// TargetLockComponent 已经恢复了正确朝向设置，此处不要用旧快照覆盖。
			if (bTargetLockIsActiveNow == bTargetLockWasActiveAtStart)
			{
				Movement->bOrientRotationToMovement =bSavedOrientRotationToMovement;
				Movement->bUseControllerDesiredRotation =bSavedUseControllerDesiredRotation;
			}
		}
	}

	bSavedMovementState = false;
	bTargetLockWasActiveAtStart = false;
	ActiveGuardMontage = nullptr;

	// 全函数只能有这一次父类结束调用。
	Super::EndAbility(Handle,ActorInfo,ActivationInfo,bReplicateEndAbility,bWasCancelled);
}