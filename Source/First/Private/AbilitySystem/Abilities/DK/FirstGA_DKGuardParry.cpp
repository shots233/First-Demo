#include "AbilitySystem/Abilities/DK/FirstGA_DKGuardParry.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "AbilitySystem/FirstAttributeSet.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/DKCharacter.h"
#include "Components/Combat/DKCombatComponent.h"
#include "Components/Combat/DKDefenseComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "MyGameplayTags.h"
#include "Types/FirstGuardHitEventData.h"

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

	if (!MeetsGuardRequirements(ActorInfo))
	{
		return false;
	}

	const UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	// 攻击中只有动画明确开放 By.Parry 取消窗口时才允许举剑。
	return !ASC->HasMatchingGameplayTag(MyGameplayTags::DK_Status_Attacking) ||
		ASC->HasMatchingGameplayTag(MyGameplayTags::DK_Status_Action_Cancelable_By_Parry);
}

bool UFirstGA_DKGuardParry::MeetsGuardRequirements(const FGameplayAbilityActorInfo* ActorInfo) const
{
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

	return true;
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
	bGuardMontageCompleted = false;
	bChainWindowConsumed = false;
	const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle);
	bGuardInputHeld = Spec && Spec->InputPressed;
	ClearBufferedParryInput();
	CloseParryChainWindow();
	ActiveGuardMontage = DK->GetGuardParryMontage();

	if (UCharacterMovementComponent* Movement = DK->GetCharacterMovement())
	{
		bSavedMovementState = true;

		Movement->StopMovementImmediately();
		Movement->MaxWalkSpeed = Defense->GetGuardMoveSpeed();
		DK->SetControllerFacingOverride(this, true);
	}

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

	// Notify 只报告动画窗口；标签、输入缓冲和消费状态都属于本技能实例。
	UAbilityTask_WaitGameplayEvent* ChainOpenTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this, MyGameplayTags::DK_Event_ParryChainWindow_Open, nullptr, false, true);
	ChainOpenTask->EventReceived.AddDynamic(this, &ThisClass::HandleParryChainWindowOpened);
	ChainOpenTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* ChainCloseTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this, MyGameplayTags::DK_Event_ParryChainWindow_Close, nullptr, false, true);
	ChainCloseTask->EventReceived.AddDynamic(this, &ThisClass::HandleParryChainWindowClosed);
	ChainCloseTask->ReadyForActivation();

	ReopenParryWindow();
	if (!ActiveGuardMontage)
	{
		// 没有表现资源时仍保留完整判定；计时到期后按是否仍按住决定退出。
		return;
	}

	GuardAnimInstance = DK->GetMesh() ? DK->GetMesh()->GetAnimInstance() : nullptr;
	if (UAnimInstance* AnimInstance = GuardAnimInstance.Get())
	{
		AnimInstance->OnMontageSectionChanged.AddUniqueDynamic(this, &ThisClass::HandleMontageSectionChanged);
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
	if (!IsActive())
	{
		return;
	}

	// 成功演出可以先结束，但退出状态不能阻止已接受的完整判定按时清理。
	RemoveParryWindowTag();
	if (bGuardMontageCompleted || (bParryRiposteActive && !ActiveGuardMontage))
	{
		FinishGuard(false);
	}
	else if (!bExitRequested && (!bGuardInputHeld || !bOwnsBlockingTag) && !bParryRiposteActive)
	{
		BeginGuardExit();
	}
}

void UFirstGA_DKGuardParry::InputPressed(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	Super::InputPressed(Handle, ActorInfo, ActivationInfo);
	if (!IsActive() || bGuardInputHeld)
	{
		return;
	}
	bGuardInputHeld = true;

	if (bExitRequested || bGuardMontageCompleted)
	{
		return;
	}

	const ADKCharacter* DK = ActorInfo ? Cast<ADKCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	const UDKDefenseComponent* Defense = DK ? DK->GetDKDefenseComponent() : nullptr;
	if (!Defense || !MeetsGuardRequirements(ActorInfo))
	{
		return;
	}

	// 松开再按住可以恢复普通格挡；这一步本身不刷新弹反计时。
	if (!bOwnsBlockingTag)
	{
		GetFirstAbilitySystemComponentFromActorInfo()->AddLooseGameplayTag(MyGameplayTags::DK_Status_Blocking);
		bOwnsBlockingTag = true;
	}
	UpdateParryMontageExit();
	// 只有成功演出允许接下一次弹反；持续按住不会产生新的按下边沿。
	if (!bParryRiposteActive || bChainWindowConsumed)
	{
		return;
	}

	const float BufferDuration = FMath::Max(0.f, Defense->GetParryInputBufferDuration());
	if (ActiveChainWindowSources.IsEmpty() && BufferDuration <= 0.f)
	{
		return;
	}
	// 仅保存最近一次点按。松开不清缓存，消费后也不会自动排队下一轮。
	BufferedParryInputExpiresAt = GetWorld()->GetTimeSeconds() + BufferDuration;
	TryConsumeBufferedParryInput();
}

void UFirstGA_DKGuardParry::InputReleased(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	Super::InputReleased(Handle, ActorInfo, ActivationInfo);
	if (!IsActive() || bExitRequested)
	{
		return;
	}
	bGuardInputHeld = false;
	// 松开只停止持续格挡；本次按下取得的完整弹反窗口仍由唯一计时器关闭。
	RemoveBlockingTag();
	StopGuardPushback();
	UpdateParryMontageExit();
	if (!bOwnsParryWindowTag && !bParryRiposteActive)
	{
		BeginGuardExit();
	}
}

void UFirstGA_DKGuardParry::HandleGuardHit(FGameplayEventData Payload)
{
	// 反击演出期间不再跳 Hit 段：格挡数值照常结算，表现保持 Parry 段完整。
	if (IsActive() && !bExitRequested && !bParryRiposteActive)
	{
		JumpToGuardSection(TEXT("Hit"));
		StartGuardPushback(Payload);
	}
}

void UFirstGA_DKGuardParry::StartGuardPushback(const FGameplayEventData& Payload)
{
	// 新的一击替换剩余推退，不累加速度或排队补位移。
	StopGuardPushback();
	ADKCharacter* DK = GetDKCharacterFromActorInfo();
	UCharacterMovementComponent* Movement = DK ? DK->GetCharacterMovement() : nullptr;
	const UFirstGuardHitEventData* HitData = Cast<UFirstGuardHitEventData>(Payload.OptionalObject.Get());
	if (!IsActive() || bExitRequested || bParryRiposteActive || !bOwnsBlockingTag ||
		!Movement || !Movement->IsMovingOnGround() || !HitData ||
		!FMath::IsFinite(HitData->GuardPushbackDistance) || !FMath::IsFinite(HitData->GuardPushbackDuration) ||
		HitData->GuardPushbackDistance <= 0.f || HitData->GuardPushbackDuration <= 0.f)
	{
		return;
	}

	FVector Direction = Payload.Instigator
		? (DK->GetActorLocation() - Payload.Instigator->GetActorLocation()).GetSafeNormal2D()
		: FVector::ZeroVector;
	if (Direction.IsNearlyZero())
	{
		Direction = -DK->GetActorForwardVector().GetSafeNormal2D();
	}

	const float Duration = FMath::Max(HitData->GuardPushbackDuration, 0.001f);
	TSharedPtr<FRootMotionSource_ConstantForce> Pushback = MakeShared<FRootMotionSource_ConstantForce>();
	Pushback->InstanceName = TEXT("FirstGuardPushback");
	Pushback->AccumulateMode = ERootMotionAccumulateMode::Override;
	Pushback->Priority = 5;
	Pushback->Force = Direction * (HitData->GuardPushbackDistance / Duration);
	Pushback->Duration = Duration;
	// 让 CharacterMovement 处理地面、台阶和碰撞，不用 LaunchCharacter 切进腾空。
	Pushback->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
	// 最后一帧只施加剩余时长的位移，避免低帧率时超出配置距离。
	Pushback->Settings.UnSetFlag(ERootMotionSourceSettingsFlags::DisablePartialEndTick);
	Pushback->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::ClampVelocity;
	Pushback->FinishVelocityParams.ClampVelocity = 0.f;
	GuardPushbackSourceID = Movement->ApplyRootMotionSource(Pushback);
}

void UFirstGA_DKGuardParry::StopGuardPushback()
{
	if (!GuardPushbackSourceID.IsSet())
	{
		return;
	}
	ADKCharacter* DK = GetDKCharacterFromActorInfo();
	UCharacterMovementComponent* Movement = DK ? DK->GetCharacterMovement() : nullptr;
	const TSharedPtr<FRootMotionSource> Pushback = Movement
		? Movement->GetRootMotionSourceByID(GuardPushbackSourceID.GetValue()) : nullptr;
	if (Pushback.IsValid() && Pushback->InstanceName == TEXT("FirstGuardPushback"))
	{
		// 移除在下一次移动更新才生效；现在收掉残余水平速度，
		// 同时禁用延迟结束时的归零，避免覆盖随后启动的闪避速度。
		Pushback->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::MaintainLastRootMotionVelocity;
		// 动画或碰撞回调可能发生在本帧源清理之后，立即清掉待应用的推力。
		StaticCastSharedPtr<FRootMotionSource_ConstantForce>(Pushback)->Force = FVector::ZeroVector;
		Pushback->RootMotionParams.Clear();
		// 清零的旧 Override 也会占住优先级；改为零贡献 Additive，让同帧新推退生效。
		Pushback->AccumulateMode = ERootMotionAccumulateMode::Additive;
		Movement->RemoveRootMotionSourceByID(GuardPushbackSourceID.GetValue());
		if (Pushback->GetTime() > 0.f)
		{
			Movement->Velocity.X = 0.f;
			Movement->Velocity.Y = 0.f;
		}
	}
	GuardPushbackSourceID.Reset();
}

void UFirstGA_DKGuardParry::HandleParrySuccess(FGameplayEventData Payload)
{
	StopGuardPushback();
	if (IsActive() && !bExitRequested && !bParryRiposteActive)
	{
		// 同一个 0.15 秒窗口内的多次成功仍结算防御，但不反复重播 Parry。
		bParryRiposteActive = true;
		bChainWindowConsumed = false;
		ClearBufferedParryInput();
		CloseParryChainWindow();
		UpdateParryMontageExit();
		JumpToGuardSection(TEXT("Parry"));
	}
}

void UFirstGA_DKGuardParry::UpdateParryMontageExit()
{
	UAnimInstance* AnimInstance = GuardAnimInstance.Get();
	if (!IsActive() || bExitRequested || !bParryRiposteActive || !AnimInstance || !ActiveGuardMontage)
	{
		return;
	}

	// Parry 已经自行放下剑。松键时直接结束，不能接从举剑姿势开始的 Block_Out。
	// 只修改本次播放实例；演出中再次按下/松开也可以更新出口。
	FName NextSection = NAME_None;
	if (bGuardInputHeld && bOwnsBlockingTag && ActiveGuardMontage->IsValidSectionName(TEXT("Loop")))
	{
		NextSection = TEXT("Loop");
	}
	AnimInstance->Montage_SetNextSection(TEXT("Parry"), NextSection, ActiveGuardMontage);

	if (FAnimMontageInstance* Instance = AnimInstance->GetActiveInstanceForMontage(ActiveGuardMontage))
	{
		if (ParryPlaybackInstanceID != Instance->GetInstanceID())
		{
			RestoreParryAutoBlendOut();
			ParryPlaybackInstanceID = Instance->GetInstanceID();
			bSavedParryAutoBlendOut = Instance->bEnableAutoBlendOut;
		}
		// 资产的 -1 会提前一个 BlendOut 时长结束活动状态，截断末尾连锁。
		// 保留完整 Parry，实际到段尾后再按原混出参数淡回移动姿势。
		Instance->bEnableAutoBlendOut = false;
		GetWorld()->GetTimerManager().SetTimer(ParryPlaybackEndTimerHandle, this,
			&ThisClass::HandleParryPlaybackEnd, 0.01f, true);
	}
}

void UFirstGA_DKGuardParry::HandleParryPlaybackEnd()
{
	UAnimInstance* AnimInstance = GuardAnimInstance.Get();
	FAnimMontageInstance* Instance = AnimInstance
		? AnimInstance->GetMontageInstanceForID(ParryPlaybackInstanceID) : nullptr;
	if (!IsActive() || bExitRequested || !bParryRiposteActive || !Instance ||
		Instance->Montage != ActiveGuardMontage || Instance->IsStopped() ||
		Instance->GetCurrentSection() != TEXT("Parry"))
	{
		RestoreParryAutoBlendOut();
		return;
	}

	float SectionStart = 0.f;
	float SectionEnd = 0.f;
	ActiveGuardMontage->GetSectionStartAndEndTime(
		ActiveGuardMontage->GetSectionIndex(TEXT("Parry")), SectionStart, SectionEnd);
	// 用实际播放位置判断，不按固定秒数猜动画结束；暂停在中间不算完成。
	if (Instance->IsPlaying() || Instance->GetPosition() < SectionEnd - UE_KINDA_SMALL_NUMBER)
	{
		return;
	}

	RestoreParryAutoBlendOut();
	bParryRiposteActive = false;
	bExitRequested = true;
	CloseParryChainWindow();
	ClearBufferedParryInput();
	RemoveBlockingTag();
	// false 表示正常结束；已接受的判定仍由原来的计时器保障完整时长。
	Instance->Stop(FAlphaBlend(ActiveGuardMontage->BlendOut,
		ActiveGuardMontage->BlendOut.GetBlendTime() * Instance->DefaultBlendTimeMultiplier), false);
}

void UFirstGA_DKGuardParry::RestoreParryAutoBlendOut()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ParryPlaybackEndTimerHandle);
	}
	if (UAnimInstance* AnimInstance = GuardAnimInstance.Get())
	{
		if (FAnimMontageInstance* Instance = AnimInstance->GetMontageInstanceForID(ParryPlaybackInstanceID);
			Instance && Instance->Montage == ActiveGuardMontage)
		{
			Instance->bEnableAutoBlendOut = bSavedParryAutoBlendOut;
		}
	}
	ParryPlaybackInstanceID = INDEX_NONE;
}

void UFirstGA_DKGuardParry::HandleParryChainWindowOpened(FGameplayEventData Payload)
{
	// 原有 ANS 继续放在 GuardParry 蒙太奇的 Parry Section 中。
	if (!IsActive() || bExitRequested || !bParryRiposteActive || bChainWindowConsumed ||
		bGuardMontageCompleted || !Payload.OptionalObject || !ActiveGuardMontage ||
		Payload.OptionalObject2.Get() != ActiveGuardMontage.Get())
	{
		return;
	}

	const ADKCharacter* DK = GetDKCharacterFromActorInfo();
	const UAnimInstance* AnimInstance = DK && DK->GetMesh() ? DK->GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance || AnimInstance->Montage_GetCurrentSection(ActiveGuardMontage) != TEXT("Parry"))
	{
		return;
	}

	ActiveChainWindowSources.Add(TWeakObjectPtr<const UObject>(Payload.OptionalObject.Get()));
	if (!bOwnsParryChainWindowTag)
	{
		GetFirstAbilitySystemComponentFromActorInfo()->AddLooseGameplayTag(MyGameplayTags::DK_Status_ParryChainWindow);
		bOwnsParryChainWindowTag = true;
	}
	TryConsumeBufferedParryInput();
}

void UFirstGA_DKGuardParry::HandleParryChainWindowClosed(FGameplayEventData Payload)
{
	if (Payload.OptionalObject2.Get() != ActiveGuardMontage.Get() || !Payload.OptionalObject)
	{
		return;
	}
	ActiveChainWindowSources.Remove(TWeakObjectPtr<const UObject>(Payload.OptionalObject.Get()));
	if (ActiveChainWindowSources.IsEmpty())
	{
		CloseParryChainWindow();
	}
}

void UFirstGA_DKGuardParry::ClearBufferedParryInput()
{
	BufferedParryInputExpiresAt = -1.0;
}

void UFirstGA_DKGuardParry::CloseParryChainWindow()
{
	ActiveChainWindowSources.Reset();
	if (bOwnsParryChainWindowTag)
	{
		if (UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponentFromActorInfo())
		{
			ASC->RemoveLooseGameplayTag(MyGameplayTags::DK_Status_ParryChainWindow);
		}
		bOwnsParryChainWindowTag = false;
	}
}

void UFirstGA_DKGuardParry::TryConsumeBufferedParryInput()
{
	if (!IsActive() || bExitRequested || !bParryRiposteActive || bChainWindowConsumed ||
		bGuardMontageCompleted || ActiveChainWindowSources.IsEmpty() || BufferedParryInputExpiresAt < 0.0)
	{
		return;
	}
	if (GetWorld()->GetTimeSeconds() > BufferedParryInputExpiresAt || !MeetsGuardRequirements(CurrentActorInfo))
	{
		ClearBufferedParryInput();
		return;
	}

	// 必须在跳段前消费权限；不能依赖下一次动画更新才派发的 NotifyEnd。
	bChainWindowConsumed = true;
	CloseParryChainWindow();
	ClearBufferedParryInput();
	bParryRiposteActive = false;
	RestoreParryAutoBlendOut();
	if (!JumpToGuardSection(TEXT("Start")))
	{
		FinishGuard(true);
		return;
	}
	if (IsActive() && !bExitRequested)
	{
		// 提前输入即使已经松开，也从实际接招时刻获得完整判定。
		ReopenParryWindow();
	}
}

void UFirstGA_DKGuardParry::ReopenParryWindow()
{
	StopGuardPushback();
	UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponentFromActorInfo();
	ADKCharacter* DK = GetDKCharacterFromActorInfo();
	UDKDefenseComponent* Defense = DK ? DK->GetDKDefenseComponent() : nullptr;
	if (!ASC || !Defense)
	{
		return;
	}

	GetWorld()->GetTimerManager().ClearTimer(ParryWindowTimerHandle);
	if (!bOwnsParryWindowTag)
	{
		ASC->AddLooseGameplayTag(MyGameplayTags::DK_Status_ParryWindow);
		bOwnsParryWindowTag = true;
	}
	if (bGuardInputHeld && !bOwnsBlockingTag)
	{
		ASC->AddLooseGameplayTag(MyGameplayTags::DK_Status_Blocking);
		bOwnsBlockingTag = true;
	}
	GetWorld()->GetTimerManager().SetTimer(ParryWindowTimerHandle,
		this, &ThisClass::HandleParryWindowElapsed,
		FMath::Max(Defense->GetParryWindowDuration(), 0.001f), false);
}

void UFirstGA_DKGuardParry::HandleMontageCompleted()
{
	RestoreParryAutoBlendOut();
	bGuardMontageCompleted = true;
	CloseParryChainWindow();
	ClearBufferedParryInput();
	// 即使表现资源较短，自然播放完毕也不截断已经接受的点按判定。
	if (!bOwnsParryWindowTag)
	{
		FinishGuard(false);
	}
}

void UFirstGA_DKGuardParry::HandleMontageInterrupted()
{
	FinishGuard(true);
}

void UFirstGA_DKGuardParry::HandleMontageSectionChanged(UAnimMontage* Montage, FName SectionName, bool bLooped)
{
	if (!IsActive() || bExitRequested || Montage != ActiveGuardMontage ||
		!bParryRiposteActive || SectionName == TEXT("Parry"))
	{
		return;
	}
	const UAnimInstance* AnimInstance = GuardAnimInstance.Get();
	// 跳段事件可能排队派发，只处理仍与实际播放位置一致的事件。
	if (!AnimInstance || AnimInstance->Montage_GetCurrentSection(Montage) != SectionName)
	{
		return;
	}
	RestoreParryAutoBlendOut();
	bParryRiposteActive = false;
	CloseParryChainWindow();
	ClearBufferedParryInput();
	if (SectionName == TEXT("End"))
	{
		// 已自然进入收势，不再跳一次 End，也不因未到期的测试判定返回格挡。
		bExitRequested = true;
		StopGuardPushback();
		RemoveBlockingTag();
		return;
	}
	if ((!bGuardInputHeld || !bOwnsBlockingTag) && !bOwnsParryWindowTag)
	{
		BeginGuardExit();
	}
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

bool UFirstGA_DKGuardParry::JumpToGuardSection(FName SectionName)
{
	ADKCharacter* DK = GetDKCharacterFromActorInfo();
	UAnimInstance* AnimInstance =DK && DK->GetMesh() ? DK->GetMesh()->GetAnimInstance() : nullptr;

	if (!AnimInstance || !ActiveGuardMontage || !AnimInstance->Montage_IsPlaying(ActiveGuardMontage) ||
		ActiveGuardMontage->GetSectionIndex(SectionName) == INDEX_NONE)
	{
		return false;
	}

	AnimInstance->Montage_JumpToSection(SectionName, ActiveGuardMontage);
	return true;
}

void UFirstGA_DKGuardParry::BeginGuardExit()
{
	if (!IsActive() || bExitRequested || bOwnsParryWindowTag || bParryRiposteActive)
	{
		return;
	}

	bExitRequested = true;
	StopGuardPushback();
	CloseParryChainWindow();
	ClearBufferedParryInput();
	RemoveBlockingTag();

	if (!JumpToGuardSection(TEXT("End")))
	{
		FinishGuard(false);
	}
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
	// 先关接招入口，防止结束蒙太奇时的通知回调重新开放旧窗口。
	bExitRequested = true;
	RestoreParryAutoBlendOut();
	StopGuardPushback();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ParryWindowTimerHandle);
	}
	if (UAnimInstance* AnimInstance = GuardAnimInstance.Get())
	{
		AnimInstance->OnMontageSectionChanged.RemoveDynamic(this, &ThisClass::HandleMontageSectionChanged);
	}
	GuardAnimInstance.Reset();
	CloseParryChainWindow();
	ClearBufferedParryInput();
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
		}
		// 只释放格挡自己的请求；锁定仍在时继续保持面向目标。
		DK->SetControllerFacingOverride(this, false);
	}

	bSavedMovementState = false;
	bGuardInputHeld = false;
	bParryRiposteActive = false;
	bChainWindowConsumed = false;
	bGuardMontageCompleted = false;
	ActiveGuardMontage = nullptr;

	// 全函数只能有这一次父类结束调用。
	Super::EndAbility(Handle,ActorInfo,ActivationInfo,bReplicateEndAbility,bWasCancelled);
}
