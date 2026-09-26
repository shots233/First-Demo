#include "Components/Locomotion/DKTurnInPlaceComponent.h"

#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Character/DKCharacter.h"
#include "Character/EnemyCharacter.h"
#include "Components/Combat/DKCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/Targeting/DKTargetLockComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MotionWarpingComponent.h"
#include "MyGameplayTags.h"
#include "RootMotionModifier_SkewWarp.h"

namespace
{
	const FName TurnWarpTarget(TEXT("DKTurnInPlaceTarget"));
	const FGameplayTagContainer& BlockingTags()
	{
		static const FGameplayTagContainer Tags = []
		{
			FGameplayTagContainer Result;
			Result.AddTag(MyGameplayTags::DK_Status_Attacking);
			Result.AddTag(MyGameplayTags::DK_Status_Dodging);
			Result.AddTag(MyGameplayTags::DK_Status_Defending);
			Result.AddTag(MyGameplayTags::DK_Status_GuardBroken);
			Result.AddTag(MyGameplayTags::DK_Status_Executing);
			Result.AddTag(MyGameplayTags::DK_Status_HitReact);
			Result.AddTag(MyGameplayTags::DK_Status_ChangingWeapon);
			Result.AddTag(MyGameplayTags::Shared_Status_Dead);
			return Result;
		}();
		return Tags;
	}
}

UDKTurnInPlaceComponent::UDKTurnInPlaceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UDKTurnInPlaceComponent::BeginPlay()
{
	Super::BeginPlay();
	Character = Cast<ADKCharacter>(GetOwner());
	if (!Character) { SetComponentTickEnabled(false); return; }
	// 先决定谁控制旋转，再由 CharacterMovement 提取本帧根运动。
	Character->GetCharacterMovement()->AddTickPrerequisiteComponent(this);
	if (UDKTargetLockComponent* Lock = Character->GetTargetLockComponent())
	{
		Lock->OnTargetLockChanged.AddUniqueDynamic(this, &ThisClass::HandleTargetChanged);
	}
	if (UFirstAbilitySystemComponent* ASC = Character->GetFirstAbilitySystemComponent())
	{
		ActionTagHandle = ASC->RegisterGenericGameplayTagEvent().AddUObject(this, &ThisClass::HandleActionTagChanged);
	}
}

void UDKTurnInPlaceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	InterruptTurn();
	if (Character)
	{
		Character->GetCharacterMovement()->RemoveTickPrerequisiteComponent(this);
		if (UDKTargetLockComponent* Lock = Character->GetTargetLockComponent())
		{
			Lock->OnTargetLockChanged.RemoveDynamic(this, &ThisClass::HandleTargetChanged);
		}
		if (UFirstAbilitySystemComponent* ASC = Character->GetFirstAbilitySystemComponent())
		{
			ASC->RegisterGenericGameplayTagEvent().Remove(ActionTagHandle);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void UDKTurnInPlaceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateTurn(DeltaTime);
}

bool UDKTurnInPlaceComponent::HasBlockingAction() const
{
	const UFirstAbilitySystemComponent* ASC = Character ? Character->GetFirstAbilitySystemComponent() : nullptr;
	return !ASC || ASC->HasAnyMatchingGameplayTags(BlockingTags());
}

bool UDKTurnInPlaceComponent::HasUsableAnimations() const
{
	const UAnimInstance* Anim = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (!Anim || Anim->RootMotionMode != ERootMotionMode::RootMotionFromMontagesOnly) { return false; }
	for (const UAnimMontage* Montage : {TurnLeft90.Get(), TurnRight90.Get(), TurnLeft180.Get(), TurnRight180.Get()})
	{
		if (!Montage || !Montage->HasRootMotion() || Montage->GetPlayLength() <= UE_SMALL_NUMBER ||
			!Anim->CurrentSkeleton || Anim->CurrentSkeleton != Montage->GetSkeleton())
		{
			return false;
		}
	}
	return true;
}

bool UDKTurnInPlaceComponent::CanManageStandingRotation() const
{
	if (!bEnabled || !Character || !HasUsableAnimations() || HasBlockingAction()) { return false; }
	const UDKTargetLockComponent* Lock = Character->GetTargetLockComponent();
	if (Lock && Lock->IsTargetLocked()) { return false; }
	const UDKCombatComponent* Combat = Character->GetDKCombatComponent();
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!Combat || Combat->CurrentEquippedWeaponTag != MyGameplayTags::DK_Weapon_Sword ||
		!Movement || !Movement->IsMovingOnGround() || Character->bPressedJump ||
		Character->GetVelocity().SizeSquared2D() > FMath::Square(FMath::Max(0.f, StationarySpeed)) ||
		!Character->GetPendingMovementInputVector().IsNearlyZero() ||
		!Character->GetLastMovementInputVector().IsNearlyZero() ||
		!Movement->GetCurrentAcceleration().IsNearlyZero()) { return false; }
	const UAnimInstance* Anim = Character->GetMesh()->GetAnimInstance();
	// 自己的转身可以继续；其他蒙太奇（包括非 GAS 的表现）拥有更高优先级。
	return ActiveMontage ? Anim->GetCurrentActiveMontage() == ActiveMontage ||
		Anim->GetCurrentActiveMontage() == nullptr : !Anim->IsAnyMontagePlaying();
}

void UDKTurnInPlaceComponent::UpdateTurn(float DeltaTime)
{
	if (!CanManageStandingRotation()) { InterruptTurn(); return; }
	if (ActiveMontage)
	{
		UAnimInstance* Anim = ActiveAnimInstance.Get();
		if (!Anim || !Anim->GetMontageInstanceForID(ActiveMontageInstanceID)) { FinishTurn(false); }
		return;
	}
	StationaryElapsed += DeltaTime;
}

bool UDKTurnInPlaceComponent::HandleMovementInput(const FVector& WorldInput)
{
	FVector Direction = WorldInput;
	Direction.Z = 0.f;
	if (Direction.IsNearlyZero()) { ReleaseMovementInput(); return false; }
	if (!CanManageStandingRotation()) { InterruptTurn(); return false; }
	// 持续按住方向不会反复打断或重播。先完成这一次踏步，再由最新输入开始移动。
	if (ActiveMontage) { return true; }
	if (StationaryElapsed < FMath::Max(0.f, IdleDelay) || GetWorld()->GetTimeSeconds() < NextTurnTime) { return false; }
	const float TargetYaw = Direction.Rotation().Yaw;
	const float DeltaYaw = FMath::FindDeltaAngleDegrees(Character->GetActorRotation().Yaw, TargetYaw);
	if (FMath::Abs(DeltaYaw) < FMath::Clamp(StartAngle, 10.f, 120.f)) { return false; }
	SetRotationSuppressed(true);
	StartTurn(DeltaYaw, TargetYaw);
	return IsTurningInPlace();
}

void UDKTurnInPlaceComponent::ReleaseMovementInput()
{
	// 不缓存一次点按到动画结束，避免松开方向键后仍自行迈步。
	InterruptTurn();
}

void UDKTurnInPlaceComponent::StartTurn(float YawDelta, float TargetYaw)
{
	const bool bLarge = FMath::Abs(YawDelta) >= FMath::Clamp(LargeTurnAngle, 90.f, 180.f);
	UAnimMontage* Montage = YawDelta < 0.f ? (bLarge ? TurnLeft180 : TurnLeft90) : (bLarge ? TurnRight180 : TurnRight90);
	UAnimInstance* Anim = Character->GetMesh()->GetAnimInstance();
	UMotionWarpingComponent* Warping = Character->GetMotionWarpingComponent();
	if (!Anim || !Warping || !Montage) { InterruptTurn(); return; }
	// 固定本次按键给出的落脚朝向；镜头转动或切换方向键不会让踏步反复重启。
	Warping->AddOrUpdateWarpTargetFromTransform(TurnWarpTarget, FTransform(FRotator(0.f, TargetYaw, 0.f), Character->GetActorLocation()));
	if (Anim->Montage_Play(Montage, FMath::Clamp(PlayRate, 0.1f, 3.f), EMontagePlayReturnType::MontageLength, 0.f, false) <= 0.f)
	{
		Warping->RemoveWarpTarget(TurnWarpTarget);
		InterruptTurn();
		return;
	}
	ActiveMontage = Montage;
	ActiveAnimInstance = Anim;
	FAnimMontageInstance* Instance = Anim->GetActiveInstanceForMontage(Montage);
	ActiveMontageInstanceID = Instance ? Instance->GetInstanceID() : INDEX_NONE;
	ActiveModifier = URootMotionModifier_SkewWarp::AddRootMotionModifierSkewWarp(Warping, Montage, 0.f,
		Montage->GetPlayLength(), TurnWarpTarget, EWarpPointAnimProvider::None, FTransform::Identity, NAME_None,
		false, true, true, EMotionWarpRotationType::Default, EMotionWarpRotationMethod::Scale, 1.f, 720.f);
	FOnMontageBlendingOutStarted BlendDelegate;
	BlendDelegate.BindUObject(this, &ThisClass::HandleMontageBlendingOut, ActiveMontageInstanceID);
	Anim->Montage_SetBlendingOutDelegate(BlendDelegate, Montage);
	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &ThisClass::HandleMontageEnded, ActiveMontageInstanceID);
	Anim->Montage_SetEndDelegate(EndDelegate, Montage);
}

void UDKTurnInPlaceComponent::FinishTurn(bool bStopMontage)
{
	UAnimInstance* Anim = ActiveAnimInstance.Get();
	FAnimMontageInstance* Instance = Anim ? Anim->GetMontageInstanceForID(ActiveMontageInstanceID) : nullptr;
	ActiveMontage = nullptr;
	ActiveAnimInstance.Reset();
	ActiveMontageInstanceID = INDEX_NONE;
	if (URootMotionModifier* Modifier = ActiveModifier.Get()) { Modifier->SetState(ERootMotionModifierState::Disabled); }
	ActiveModifier.Reset();
	if (Character && Character->GetMotionWarpingComponent()) { Character->GetMotionWarpingComponent()->RemoveWarpTarget(TurnWarpTarget); }
	if (Instance)
	{
		Instance->OnMontageBlendingOutStarted.Unbind();
		Instance->OnMontageEnded.Unbind();
		if (bStopMontage)
		{
			// 淡出只保留姿势混合，不允许旧根旋转继续影响刚开始的移动或闪避。
			Instance->PushDisableRootMotion();
			Instance->Stop(FAlphaBlend(FMath::Max(0.f, InterruptBlendOutTime)), true);
		}
	}
	StationaryElapsed = 0.f;
	NextTurnTime = GetWorld() ? GetWorld()->GetTimeSeconds() + FMath::Max(0.f, TurnCooldown) : 0.0;
	SetRotationSuppressed(false);
}

void UDKTurnInPlaceComponent::SetRotationSuppressed(bool bSuppress)
{
	if (Character && bSuppress != bRotationSuppressed)
	{
		bRotationSuppressed = bSuppress;
		Character->SetAutomaticRotationSuppressed(this, bSuppress);
	}
}

void UDKTurnInPlaceComponent::InterruptTurn()
{
	if (ActiveMontage) { FinishTurn(true); }
	StationaryElapsed = 0.f;
	SetRotationSuppressed(false);
}

void UDKTurnInPlaceComponent::HandleActionTagChanged(FGameplayTag Tag, int32 NewCount)
{
	if (NewCount > 0 && Tag.MatchesAny(BlockingTags())) { InterruptTurn(); }
}

void UDKTurnInPlaceComponent::HandleTargetChanged(AEnemyCharacter* NewTarget)
{
	InterruptTurn();
}

void UDKTurnInPlaceComponent::HandleMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted, int32 InstanceID)
{
	// UE 可能已经把旧委托复制到待处理队列，解绑不能撤回它。
	if (InstanceID == ActiveMontageInstanceID && Montage == ActiveMontage && bInterrupted) { InterruptTurn(); }
}

void UDKTurnInPlaceComponent::HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted, int32 InstanceID)
{
	if (InstanceID == ActiveMontageInstanceID && Montage == ActiveMontage) { FinishTurn(false); }
}
