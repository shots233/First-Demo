#include "AbilitySystem/Abilities/BOSS/FirstBossPursuitAbility.h"
#include "AbilitySystem/Abilities/BOSS/FirstBossPursuitAnimation.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "AbilitySystem/GameplayEffects/FirstGE_BossCooldown.h"
#include "AbilitySystem/GameplayEffects/FirstGE_Damage.h"
#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Character/BossCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/Combat/BossCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Controller/BossAIController.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MotionWarpingComponent.h"
#include "MyGameplayTags.h"
#include "RootMotionModifier_SkewWarp.h"

DEFINE_LOG_CATEGORY_STATIC(LogBossPursuit, Log, All);

namespace FirstBossPursuit
{
	const FName WarpTargetName(TEXT("BossPursuitTarget"));
	const FName AttackWarpTargetName(TEXT("AttackTarget"));

	bool IsMontageCompatible(const ABossCharacter* Boss, const UAnimMontage* Montage)
	{
		const USkeletalMesh* Mesh = Boss && Boss->GetMesh() ? Boss->GetMesh()->GetSkeletalMeshAsset() : nullptr;
		return Mesh && Montage && Montage->GetPlayLength() > 0.f && Montage->GetSkeleton() == Mesh->GetSkeleton();
	}

	bool IsDodgeMontageUsable(const ABossCharacter* Boss, const UAnimMontage* Montage)
	{
		if (!IsMontageCompatible(Boss, Montage) || !FMath::IsFinite(Montage->GetPlayLength()) || !Montage->HasRootMotion() ||
			!Montage->bEnableAutoBlendOut || !FMath::IsFinite(Montage->RateScale) || Montage->RateScale <= 0.f ||
			Montage->SlotAnimTracks.Num() != 1 || Montage->SlotAnimTracks[0].SlotName.IsNone() ||
			Montage->CompositeSections.IsEmpty())
		{
			return false;
		}
		// 前闪只承载一段前移动作；避免混合开关不同的根运动序列后，
		// Motion Warping 预测的总位移与蒙太奇实际提取的位移不一致。
		const TArray<FAnimSegment>& Segments = Montage->SlotAnimTracks[0].AnimTrack.AnimSegments;
		if (Segments.Num() != 1) return false;
		const UAnimSequence* Sequence = Cast<UAnimSequence>(Segments[0].GetAnimReference());
		if (!Sequence || !Sequence->bEnableRootMotion || Sequence->GetSkeleton() != Montage->GetSkeleton() ||
			Segments[0].LoopingCount != 1 || !FMath::IsFinite(Segments[0].AnimPlayRate) || Segments[0].AnimPlayRate <= 0.f ||
			!FMath::IsFinite(Sequence->RateScale) || Sequence->RateScale <= 0.f)
		{
			return false;
		}
		// 本次固定落点按完整、连续的时间范围计算；跳段或循环会破坏该范围。
		for (int32 Index = 0; Index < Montage->CompositeSections.Num(); ++Index)
		{
			const FCompositeSection& Section = Montage->CompositeSections[Index];
			const FName ExpectedNext = Montage->CompositeSections.IsValidIndex(Index + 1)
				? Montage->CompositeSections[Index + 1].SectionName : NAME_None;
			if (Section.NextSectionName != ExpectedNext ||
				(Index == 0 && !FMath::IsNearlyZero(Section.GetTime())) ||
				(Index > 0 && Section.GetTime() <= Montage->CompositeSections[Index - 1].GetTime()))
			{
				return false;
			}
		}
		const FVector RootTravel = Montage->ExtractRootMotionFromTrackRange(0.f, Montage->GetPlayLength(),
			FAnimExtractContext(0.0, true)).GetTranslation();
		return !RootTravel.ContainsNaN() && RootTravel.SizeSquared2D() > 1.f;
	}
}

UFirstBossPursuitAbility::UFirstBossPursuitAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::Boss_Ability_Attack);
	AssetTags.AddTag(MyGameplayTags::Boss_Ability_Attack_Pursuit);
	SetAssetTags(AssetTags);
	ActivationRequiredTags.AddTag(MyGameplayTags::Boss_Status_WeaponDrawn);
	ActivationOwnedTags.AddTag(MyGameplayTags::Boss_Status_Attacking);
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Attacking);
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_DrawSword);
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Staggered);
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Executable);
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_BeingExecuted);
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Executed);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);
	CooldownGameplayEffectClass = UFirstGE_BossCooldown::StaticClass();
	CooldownTags.AddTag(MyGameplayTags::Boss_Cooldown_Attack_Pursuit);
	bIsCancelable = true;
}

const FGameplayTagContainer* UFirstBossPursuitAbility::GetCooldownTags() const
{
	return &CooldownTags;
}

void UFirstBossPursuitAbility::ApplyCooldown(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
	if (CooldownDuration > 0.f) Super::ApplyCooldown(Handle, ActorInfo, ActivationInfo);
}

bool UFirstBossPursuitAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags)) return false;
	const ABossCharacter* Boss = ActorInfo ? Cast<ABossCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	return IsValid(Boss) && Boss->PursuitSettings.bEnabled && AreSettingsValid(Boss->PursuitSettings) &&
		Boss->GetCharacterMovement() && Boss->GetCharacterMovement()->IsMovingOnGround();
}

bool UFirstBossPursuitAbility::ShouldAbilityRespondToEvent(const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayEventData* Payload) const
{
	if (!Payload || !Super::ShouldAbilityRespondToEvent(ActorInfo, Payload)) return false;
	FVector Direction;
	float Travel = 0.f;
	return GetPursuitPlan(ActorInfo, Cast<ACharacter>(const_cast<AActor*>(Payload->Target.Get())), Direction, Travel);
}

void UFirstBossPursuitAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	Phase = EPhase::Dodge;
	bWasParried = bOwnsDirectionLock = bEndingPursuit = false;
	PursuitTarget = TriggerEventData ? Cast<ACharacter>(const_cast<AActor*>(TriggerEventData->Target.Get())) : nullptr;
	ActivePhaseMontage = ActiveDodgeMontage = nullptr;

	UAbilityTask_WaitGameplayEvent* ParryTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, MyGameplayTags::Boss_Event_Parried, nullptr, false, true);
	ParryTask->EventReceived.AddDynamic(this, &ThisClass::HandleParried);
	ParryTask->ReadyForActivation();
	UAbilityTask_WaitGameplayEvent* HitTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, MyGameplayTags::DK_Event_MeleeHit, nullptr, false, true);
	HitTask->EventReceived.AddDynamic(this, &ThisClass::HandleMeleeHit);
	HitTask->ReadyForActivation();
	StartPursuit();
}

bool UFirstBossPursuitAbility::IsPursuitTargetValid(ACharacter* Target) const
{
	const ABossCharacter* Boss = Cast<ABossCharacter>(GetAvatarActorFromActorInfo());
	if (!IsValid(Boss) || !IsValid(Target) || Target->IsActorBeingDestroyed() ||
		Target == Boss)
	{
		return false;
	}
	if (const UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target))
	{
		if (TargetASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead))
		{
			return false;
		}
	}
	return !(Target->GetActorLocation() - Boss->GetActorLocation()).ContainsNaN();
}

bool UFirstBossPursuitAbility::IsTargetUsable(ACharacter* Target, float MaxRange) const
{
	const ABossCharacter* Boss = Cast<ABossCharacter>(GetAvatarActorFromActorInfo());
	if (!IsPursuitTargetValid(Target) || !FMath::IsFinite(MaxRange) || MaxRange <= 0.f)
	{
		return false;
	}
	const FVector Delta = Target->GetActorLocation() - Boss->GetActorLocation();
	if (Delta.SizeSquared2D() > FMath::Square(MaxRange) ||
		FMath::Abs(Delta.Z) > Boss->PursuitSettings.MaxTargetHeightDifference)
	{
		return false;
	}
	if (ABossAIController* BossAI = Cast<ABossAIController>(Boss->GetController()))
	{
		// 复用控制器带缓存的可达性检查，与行为树及后撤蓄力斩保持一致。
		if (BossAI->RefreshTargetNavigationBlocked(Target)) return false;
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(BossPursuitVisibility), false, Boss);
	Query.AddIgnoredActor(Target);
	return !Boss->GetWorld()->LineTraceTestByChannel(Boss->GetActorLocation(), Target->GetActorLocation(),
		ECC_Visibility, Query);
}

bool UFirstBossPursuitAbility::IsPursuitAllowed() const
{
	const ABossCharacter* Boss = Cast<ABossCharacter>(GetAvatarActorFromActorInfo());
	const UFirstAbilitySystemComponent* ASC = Boss ? Boss->GetFirstAbilitySystemComponent() : nullptr;
	const UCharacterMovementComponent* Movement = Boss ? Boss->GetCharacterMovement() : nullptr;
	return IsActive() && !bEndingPursuit && !bWasParried && ASC && Boss->PursuitSettings.bEnabled &&
		Movement && Movement->IsMovingOnGround() &&
		ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_WeaponDrawn) &&
		!ASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead) &&
		!ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Staggered) &&
		!ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Executable) &&
		!ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_BeingExecuted) &&
		!ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Executed) &&
		!ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_DrawSword);
}

bool UFirstBossPursuitAbility::AreSettingsValid(const FFirstBossPursuitSettings& S) const
{
	return FMath::IsFinite(S.Cooldown) && S.Cooldown >= 0.f &&
		FMath::IsFinite(S.DirectAttackRange) && S.DirectAttackRange > 0.f &&
		FMath::IsFinite(S.AttackWarpRange) && S.AttackWarpRange >= 0.f &&
		FMath::IsFinite(S.AttackWarpMaxTravelDistance) && S.AttackWarpMaxTravelDistance >= 0.f &&
		FMath::IsFinite(S.AttackWarpSurfaceGap) && S.AttackWarpSurfaceGap >= 0.f &&
		FMath::IsFinite(S.AttackAimYawOffset) && FMath::Abs(S.AttackAimYawOffset) <= 45.f &&
		FMath::IsFinite(S.AttackWarpLateralOffset) && FMath::Abs(S.AttackWarpLateralOffset) <= 100.f &&
		FMath::IsFinite(S.MaxDashDistance) && S.MaxDashDistance > 0.f &&
		FMath::IsFinite(S.StopSurfaceGap) && S.StopSurfaceGap >= 0.f &&
		FMath::IsFinite(S.MaxStartFacingAngle) && S.MaxStartFacingAngle >= 0.f && S.MaxStartFacingAngle <= 180.f &&
		FMath::IsFinite(S.MaxAttackFacingCorrection) && S.MaxAttackFacingCorrection >= 0.f && S.MaxAttackFacingCorrection <= 180.f &&
		FMath::IsFinite(S.MaxTargetHeightDifference) && S.MaxTargetHeightDifference >= 0.f &&
		FMath::IsFinite(S.DodgePlayRate) && S.DodgePlayRate >= 0.1f &&
		(S.bUseNormalAttackDamage || (FMath::IsFinite(S.AttackDamage) && S.AttackDamage >= 0.f));
}

bool UFirstBossPursuitAbility::GetPursuitPlan(const FGameplayAbilityActorInfo* ActorInfo,
	ACharacter* Target, FVector& OutDirection, float& OutTravel) const
{
	const ABossCharacter* Boss = ActorInfo ? Cast<ABossCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	if (!IsValid(Boss) || !Boss->PursuitSettings.bEnabled || !AreSettingsValid(Boss->PursuitSettings) ||
		!Boss->GetCharacterMovement() || !Boss->GetCharacterMovement()->IsMovingOnGround()) return false;
	if (!IsValid(Target) || Target == Boss || Target->IsActorBeingDestroyed()) return false;
	if (const UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target))
	{
		if (TargetASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead)) return false;
	}
	const FFirstBossPursuitSettings& S = Boss->PursuitSettings;
	const FVector Delta = Target->GetActorLocation() - Boss->GetActorLocation();
	const float Distance = Delta.Size2D();
	if (Delta.ContainsNaN() ||
		FMath::Abs(Delta.Z) > S.MaxTargetHeightDifference) return false;
	UAnimMontage* AttackMontage = S.FollowupAttackMontage ? S.FollowupAttackMontage.Get() : Boss->GetNormalAttackMontage();
	const FVector ToTarget = Delta.GetSafeNormal2D();
	if (!FirstBossPursuit::IsMontageCompatible(Boss, AttackMontage) ||
		FMath::Abs(FMath::FindDeltaAngleDegrees(Boss->GetActorRotation().Yaw, ToTarget.Rotation().Yaw)) > S.MaxStartFacingAngle)
	{
		return false;
	}
	if (S.ForwardDodgeMontage)
	{
		if (!FirstBossPursuit::IsDodgeMontageUsable(Boss, S.ForwardDodgeMontage)) return false;
	}
	else
	{
		const UAnimSequence* Sequence = S.ForwardDodgeAnimation;
		const USkeletalMesh* Mesh = Boss->GetMesh() ? Boss->GetMesh()->GetSkeletalMeshAsset() : nullptr;
		if (!Sequence || !Mesh || Sequence->GetSkeleton() != Mesh->GetSkeleton() ||
			!Sequence->bEnableRootMotion || Sequence->GetPlayLength() <= 0.f) return false;
	}
	if (ABossAIController* BossAI = Cast<ABossAIController>(Boss->GetController()))
	{
		if (BossAI->RefreshTargetNavigationBlocked(Target)) return false;
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(BossChasePursuitVisibility), false, Boss);
	Query.AddIgnoredActor(Target);
	if (Boss->GetWorld()->LineTraceTestByChannel(Boss->GetActorLocation(), Target->GetActorLocation(), ECC_Visibility, Query)) return false;
	const float StopDistance = Boss->GetCapsuleComponent()->GetScaledCapsuleRadius() +
		Target->GetCapsuleComponent()->GetScaledCapsuleRadius() + S.StopSurfaceGap;
	const float Travel = FMath::Min(Distance - StopDistance, S.MaxDashDistance);
	if (!Boss->GetMotionWarpingComponent() || Travel <= 1.f ||
		Distance - Travel > S.DirectAttackRange || !Boss->HasSafeGroundTravel(ToTarget, Travel)) return false;
	OutDirection = ToTarget;
	OutTravel = Travel;
	return true;
}

void UFirstBossPursuitAbility::StartPursuit()
{
	if (!IsActive()) return;
	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	ACharacter* Target = PursuitTarget.Get();
	FVector ToTarget;
	float Travel = 0.f;
	if (!IsPursuitAllowed() || !GetPursuitPlan(CurrentActorInfo, Target, ToTarget, Travel))
	{
		FinishPursuit(true);
		return;
	}
	const FFirstBossPursuitSettings& S = Boss->PursuitSettings;
	ActiveDodgeMontage = S.ForwardDodgeMontage;
	if (!ActiveDodgeMontage)
	{
		UAnimSequence* Sequence = S.ForwardDodgeAnimation;
		const USkeletalMesh* Mesh = Boss->GetMesh()->GetSkeletalMeshAsset();
		if (Sequence && Mesh && Sequence->GetSkeleton() == Mesh->GetSkeleton() && Sequence->bEnableRootMotion)
		{
			FName SlotName = S.DodgeSlotName;
			if (SlotName.IsNone())
			{
				SlotName = FName(TEXT("DefaultSlot"));
			}
			ActiveDodgeMontage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(Sequence, SlotName, 0.05f, 0.1f, 1.f, 1, 0.f);
		}
	}
	if (!FirstBossPursuit::IsDodgeMontageUsable(Boss, ActiveDodgeMontage))
	{
		ActiveDodgeMontage = nullptr;
		FinishPursuit(true);
		return;
	}

	// 无效范围/路径/资源不会消费冷却，也不会停止原来的跑步追击。
	CooldownDuration = S.Cooldown;
	if (!CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo))
	{
		FinishPursuit(true);
		return;
	}
	PursuitTarget = Target;
	Boss->EndAttackWarping();
	Phase = EPhase::Dodge;
	CommittedYaw = ToTarget.Rotation().Yaw;
	DashDestination = Boss->GetActorLocation() + ToTarget * Travel;
	UE_LOG(LogBossPursuit, Verbose, TEXT("Dodge committed: distance=%.1f travel=%.1f destination=%s"),
		FVector::Dist2D(Boss->GetActorLocation(), Target->GetActorLocation()), Travel, *DashDestination.ToCompactString());
	Boss->SetActorRotation(FRotator(0.f, CommittedYaw, 0.f));
	SetDirectionLocked(true);
	UMotionWarpingComponent* Warping = Boss->GetMotionWarpingComponent();
	// 位移仍来自动画；只把其根运动缩放到本次确定的安全落点，不持续追踪玩家。
	Warping->AddOrUpdateWarpTargetFromTransform(FirstBossPursuit::WarpTargetName,
		FTransform(FRotator(0.f, CommittedYaw, 0.f), DashDestination - FVector(0.f, 0.f,
			Boss->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())));
	DashWarpModifier = URootMotionModifier_SkewWarp::AddRootMotionModifierSkewWarp(Warping,
		ActiveDodgeMontage, 0.f, ActiveDodgeMontage->GetPlayLength(), FirstBossPursuit::WarpTargetName,
		EWarpPointAnimProvider::None, FTransform::Identity, NAME_None, true, true, true);
	PlayPhaseMontage(ActiveDodgeMontage, S.DodgePlayRate);
	if (IsActive())
	{
		GetWorld()->GetTimerManager().SetTimer(DashSafetyTimer, this, &ThisClass::MonitorDash, 0.02f, true);
	}
}

void UFirstBossPursuitAbility::MonitorDash()
{
	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	// 前闪方向和落点已经确定，玩家后退/横移不撤销本次出刀。
	// 目标失效和角色受控状态仍可取消，路径安全检查也持续生效。
	if (!IsPursuitAllowed() || !IsPursuitTargetValid(PursuitTarget.Get()))
	{
		UE_LOG(LogBossPursuit, Verbose, TEXT("Dodge cancelled: status or target invalid"));
		FinishPursuit(true);
		return;
	}
	const FVector Remaining = DashDestination - Boss->GetActorLocation();
	if (Remaining.SizeSquared2D() > 4.f && !Boss->HasSafeGroundTravel(Remaining, Remaining.Size2D()))
	{
		UE_LOG(LogBossPursuit, Verbose, TEXT("Dodge cancelled: remaining route unsafe, distance=%.1f"), Remaining.Size2D());
		FinishPursuit(true);
	}
}

void UFirstBossPursuitAbility::StartFollowupAttack()
{
	if (!IsActive()) return;
	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	ACharacter* Target = PursuitTarget.Get();
	if (!IsPursuitAllowed() || !AreSettingsValid(Boss->PursuitSettings) ||
		!IsPursuitTargetValid(Target))
	{
		// 前闪后因状态/目标失效没能出刀，向等待中的行为树任务报告中断。
		FinishPursuit(true);
		return;
	}
	const FFirstBossPursuitSettings& S = Boss->PursuitSettings;
	const FVector ToTarget = (Target->GetActorLocation() - Boss->GetActorLocation()).GetSafeNormal2D();
	const float TargetYaw = ToTarget.IsNearlyZero() ? CommittedYaw : ToTarget.Rotation().Yaw;
	const float TargetYawDelta = FMath::FindDeltaAngleDegrees(CommittedYaw, TargetYaw);
	const float AttackYaw = CommittedYaw + FMath::Clamp(TargetYawDelta + S.AttackAimYawOffset,
		-S.MaxAttackFacingCorrection, S.MaxAttackFacingCorrection);
	UAnimMontage* Montage = S.FollowupAttackMontage ? S.FollowupAttackMontage.Get() : Boss->GetNormalAttackMontage();
	if (!FirstBossPursuit::IsMontageCompatible(Boss, Montage))
	{
		FinishPursuit(true);
		return;
	}
	StopCurrentPhase();
	ClearDashWarp();
	Phase = EPhase::Attack;
	Boss->SetActorRotation(FRotator(0.f, AttackYaw, 0.f));
	SetDirectionLocked(true);
	const float AttackStopDistance = Boss->GetCapsuleComponent()->GetScaledCapsuleRadius() +
		Target->GetCapsuleComponent()->GetScaledCapsuleRadius() + S.AttackWarpSurfaceGap;
	const float AttackDistance = FVector::Dist2D(Boss->GetActorLocation(), Target->GetActorLocation());
	// 横移以目标连线为基准，不随补偿后的身体朝向改变；合计位移共用原来的上限。
	const FVector TargetRight(-ToTarget.Y, ToTarget.X, 0.f);
	const FVector AttackTravelVector = (ToTarget * FMath::Max(0.f, AttackDistance - AttackStopDistance) +
		TargetRight * S.AttackWarpLateralOffset).GetClampedToMaxSize2D(S.AttackWarpMaxTravelDistance);
	const float AttackTravel = AttackTravelVector.Size2D();
	UMotionWarpingComponent* Warping = Boss->GetMotionWarpingComponent();
	const bool bWarpAttack = Warping && S.AttackWarpRange > 0.f && S.AttackWarpMaxTravelDistance > 0.f &&
		FMath::Abs(TargetYawDelta) <= S.MaxAttackFacingCorrection &&
		IsTargetUsable(Target, S.AttackWarpRange) &&
		Boss->HasSafeGroundTravel(AttackTravelVector, AttackTravel);
	// 清除旧会话及其跟踪计时器。本次只写入一次固定落点，结束或被打断时沿用原清理流程。
	Boss->EndAttackWarping();
	if (bWarpAttack)
	{
		// 蒙太奇中的 AttackTarget 通知使用相同的补偿朝向，避免吸附时把身体转回目标中心。
		Warping->AddOrUpdateWarpTargetFromTransform(FirstBossPursuit::AttackWarpTargetName,
			FTransform(FRotator(0.f, AttackYaw, 0.f), Boss->GetActorLocation() + AttackTravelVector));
	}
	// 超距、侧后方或偏移后路径不安全时仍挥刀，按锁定方向播放原动画位移。
	UE_LOG(LogBossPursuit, Verbose, TEXT("Chase followup attack: distance=%.1f targetYawDelta=%.1f appliedYawDelta=%.1f warp=%d range=%.1f travel=%.1f aimOffset=%.1f lateral=%.1f"),
		AttackDistance, TargetYawDelta, FMath::FindDeltaAngleDegrees(CommittedYaw, AttackYaw),
		bWarpAttack, S.AttackWarpRange, bWarpAttack ? AttackTravel : 0.f,
		S.AttackAimYawOffset, bWarpAttack ? FVector::DotProduct(AttackTravelVector, TargetRight) : 0.f);
	PlayPhaseMontage(Montage);
}

void UFirstBossPursuitAbility::HandleMeleeHit(FGameplayEventData Payload)
{
	if (!IsActive() || bEndingPursuit || Phase != EPhase::Attack) return;
	AActor* Target = const_cast<AActor*>(Payload.Target.Get());
	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	if (IsValid(Target) && Boss)
	{
		const FFirstBossPursuitSettings& S = Boss->PursuitSettings;
		const EFirstDefenseResult Result = ResolveTargetDefense(Target,
			S.bUseNormalAttackDefense ? Boss->GetNormalAttackDefenseData() : S.AttackDefenseData);
		// 防御结算会同步触发弹反、破韧与 CancelAbility，必须再次检查生命周期。
		if (IsActive() && Result == EFirstDefenseResult::Damaged)
		{
			PlayBossHitPlayerFeedback(Target);
			ApplyEffectSpecHandleToTarget(Target, MakeBossDamageEffectSpecHandle(UFirstGE_Damage::StaticClass(),
				S.bUseNormalAttackDamage ? Boss->GetNormalAttackDamage() : S.AttackDamage));
		}
	}
}

void UFirstBossPursuitAbility::HandleParried(FGameplayEventData Payload)
{
	if (!IsActive()) return;
	bWasParried = true;
	FinishPursuit(true);
}

void UFirstBossPursuitAbility::PlayPhaseMontage(UAnimMontage* Montage, float PlayRate)
{
	ClearMontageTask();
	ActivePhaseMontage = Montage;
	PhaseMontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, TEXT("BossPursuitPhase"), Montage, PlayRate, NAME_None, true, 1.f, 0.f, true);
	PhaseMontageTask->OnCompleted.AddDynamic(this, &ThisClass::HandlePhaseCompleted);
	PhaseMontageTask->OnInterrupted.AddDynamic(this, &ThisClass::HandlePhaseInterrupted);
	PhaseMontageTask->OnCancelled.AddDynamic(this, &ThisClass::HandlePhaseInterrupted);
	PhaseMontageTask->ReadyForActivation();
}

void UFirstBossPursuitAbility::HandlePhaseCompleted()
{
	if (!IsActive() || bEndingPursuit) return;
	UE_LOG(LogBossPursuit, Verbose, TEXT("Phase completed: %d"), static_cast<int32>(Phase));
	ClearMontageTask();
	ActivePhaseMontage = nullptr;
	if (Phase == EPhase::Dodge)
	{
		ClearDashWarp();
		StartTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::StartFollowupAttack);
	}
	else
	{
		FinishPursuit(false);
	}
}

void UFirstBossPursuitAbility::HandlePhaseInterrupted()
{
	UE_LOG(LogBossPursuit, Verbose, TEXT("Phase interrupted: %d"), static_cast<int32>(Phase));
	FinishPursuit(true);
}

void UFirstBossPursuitAbility::ClearMontageTask()
{
	if (PhaseMontageTask)
	{
		PhaseMontageTask->OnCompleted.RemoveAll(this);
		PhaseMontageTask->OnInterrupted.RemoveAll(this);
		PhaseMontageTask->OnCancelled.RemoveAll(this);
		PhaseMontageTask->EndTask();
		PhaseMontageTask = nullptr;
	}
}

void UFirstBossPursuitAbility::StopCurrentPhase()
{
	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	UAnimInstance* Anim = Boss && Boss->GetMesh() ? Boss->GetMesh()->GetAnimInstance() : nullptr;
	UAnimMontage* PlayingMontage = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
	const bool bStopDashVelocity = Phase == EPhase::Dodge && bOwnsDirectionLock && ASC &&
		(ASC->GetAnimatingAbility() == this || (!ASC->GetAnimatingAbility() &&
			(!PlayingMontage || PlayingMontage == ActiveDodgeMontage)));
	ClearMontageTask();
	if (ActivePhaseMontage && ASC && ASC->GetAnimatingAbility() == this)
	{
		// 先解除旧任务再停止，复用同一普通攻击 Montage 时也不会误取消新阶段。
		ASC->CurrentMontageStop(Phase == EPhase::Dodge ? 0.f : 0.08f);
		ASC->ClearAnimatingAbility(this);
	}
	if (bStopDashVelocity && Boss)
	{
		// 路径被否决后立即止步，避免上一帧根运动速度继续滑行。
		// 若其它能力已接管动画，则不清除它刚写入的速度。
		if (Anim && PlayingMontage == ActiveDodgeMontage) Anim->Montage_Stop(0.f, ActiveDodgeMontage);
		Boss->GetCharacterMovement()->StopMovementImmediately();
	}
	ActivePhaseMontage = nullptr;
	if (UBossCombatComponent* Combat = GetBossCombatComponentFromActorInfo()) Combat->ToggleWeaponCollision(false);
}

void UFirstBossPursuitAbility::ClearDashWarp()
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(DashSafetyTimer);
	if (DashWarpModifier)
	{
		DashWarpModifier->SetState(ERootMotionModifierState::MarkedForRemoval);
		DashWarpModifier = nullptr;
	}
	if (ABossCharacter* Boss = GetBossCharacterFromActorInfo())
	{
		if (UMotionWarpingComponent* Warping = Boss->GetMotionWarpingComponent())
		{
			Warping->RemoveWarpTarget(FirstBossPursuit::WarpTargetName);
		}
	}
}

void UFirstBossPursuitAbility::SetDirectionLocked(bool bLocked)
{
	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	UFirstAbilitySystemComponent* ASC = Boss ? Boss->GetFirstAbilitySystemComponent() : nullptr;
	if (!ASC) return;
	if (bLocked)
	{
		if (!bOwnsDirectionLock)
		{
			bOwnsDirectionLock = true;
			ASC->AddLooseGameplayTag(MyGameplayTags::Boss_Status_AttackDirectionLocked);
		}
		Boss->SetBossRotationMode(EBossRotationMode::Frozen);
		if (AAIController* Controller = Cast<AAIController>(Boss->GetController()))
		{
			Controller->StopMovement();
			Controller->ClearFocus(EAIFocusPriority::Gameplay);
		}
		Boss->GetCharacterMovement()->StopMovementImmediately();
	}
	else if (bOwnsDirectionLock)
	{
		bOwnsDirectionLock = false;
		ASC->RemoveLooseGameplayTag(MyGameplayTags::Boss_Status_AttackDirectionLocked);
		// 死亡、处决或其它能力接管旋转时不能恢复旧模式。
		if (!ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_AttackDirectionLocked) &&
			!ASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead) &&
			!ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Staggered) &&
			!ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Executable) &&
			!ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_BeingExecuted) &&
			!ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Executed))
		{
			// 结束后让 AI 状态服务根据当前目标重新选择朝向，不恢复技能捕获的旧目标。
			Boss->SetBossRotationMode(EBossRotationMode::OrientToMovement);
		}
	}
}

void UFirstBossPursuitAbility::FinishPursuit(bool bCancelled)
{
	if (IsActive() && !bEndingPursuit)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bCancelled);
	}
}

void UFirstBossPursuitAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (bEndingPursuit || !IsEndAbilityValid(Handle, ActorInfo)) return;
	// GAS 有时会推迟 EndAbility 到能力锁释放后；此时不能先置清理完成标记。
	if (ScopeLockCount > 0)
	{
		Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
		return;
	}
	bEndingPursuit = true;
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(StartTimer);
	StopCurrentPhase();
	ClearDashWarp();
	if (ABossCharacter* Boss = GetBossCharacterFromActorInfo()) Boss->EndAttackWarping();
	SetDirectionLocked(false);
	PursuitTarget.Reset();
	ActiveDodgeMontage = nullptr;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
