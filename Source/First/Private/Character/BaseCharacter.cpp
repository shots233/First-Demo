// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/BaseCharacter.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "AbilitySystem/FirstAttributeSet.h"
#include "Components/CapsuleComponent.h"
#include "DataAssets/StartUpData/DataAsset_StartUpDataBase.h"
#include "MotionWarpingComponent.h"
#include "MyGameplayTags.h"
#include "RootMotionModifier.h"
#include "Types/FirstCombatTypes.h"

namespace
{
	const FName AttackWarpTargetName(TEXT("AttackTarget"));
}

// Sets default values
ABaseCharacter::ABaseCharacter()
{
	//彻底关闭了这个Actor的Tick功能
	PrimaryActorTick.bCanEverTick = false;
	//确保Actor刚诞生时Tick是关闭的
	PrimaryActorTick.bStartWithTickEnabled = false;
	
	FirstAbilitySystemComponent = CreateDefaultSubobject<UFirstAbilitySystemComponent>(TEXT("FirstAbilitySystemComponent"));
	// AttributeSet 作为 ASC 所在角色的默认子对象创建，生命周期与角色一致。
	FirstAttributeSet = CreateDefaultSubobject<UFirstAttributeSet>(TEXT("FirstAttributeSet"));

	// 近战吸附组件：攻击时由 GA 调用 AddOrUpdateWarpTarget 设置目标。
	MotionWarpingComponent = CreateDefaultSubobject<UMotionWarpingComponent>(TEXT("MotionWarpingComponent"));
}

void ABaseCharacter::BeginAttackWarping(ACharacter* Target, const FFirstAttackWarpingData& WarpingData)
{
	// 新攻击段开始前先销毁旧会话，避免失锁、超距或换目标时复用残留的 AttackTarget。
	EndAttackWarping();

	if (!IsAttackWarpTargetUsable(Target))
	{
		return;
	}

	AttackWarpTargetActor = Target;
	AttackWarpMaxDistance = FMath::Max(0.f, WarpingData.MaxWarpDistance);
	AttackWarpSurfaceGap = FMath::Max(0.f, WarpingData.SurfaceGap);
	AttackWarpMaxTravelDistance = FMath::Max(0.f, WarpingData.MaxWarpTravelDistance);
	bKeepAttackWarpTargetWhenBeyondMaxDistance =
		WarpingData.bKeepWarpTargetWhenBeyondMaxDistance;
	AttackFacingTurnRate = FMath::Max(0.f, WarpingData.FacingTurnRate);
	AttackMaxFacingAngle = FMath::Clamp(WarpingData.MaxFacingAngle, 0.f, 180.f);
	AttackFacingOriginYaw = GetActorRotation().Yaw;

	// Montage 播放前先准备好目标，确保第一帧进入 Warp 窗口时就能读取到正确 Transform。
	UpdateAttackWarping();

	if (AttackWarpTargetActor.IsValid() && WarpingData.bTrackTarget)
	{
		GetWorldTimerManager().SetTimer(
			AttackWarpUpdateTimerHandle,
			this,
			&ThisClass::UpdateAttackWarping,
			FMath::Max(0.005f, WarpingData.UpdateInterval),
			true);
	}
}

void ABaseCharacter::EndAttackWarping()
{
	GetWorldTimerManager().ClearTimer(AttackWarpUpdateTimerHandle);
	AttackWarpTargetActor.Reset();
	bKeepAttackWarpTargetWhenBeyondMaxDistance = false;

	if (MotionWarpingComponent)
	{
		// 只删除攻击使用的目标，不能破坏处决等系统以后可能使用的其他 WarpTarget。
		MotionWarpingComponent->RemoveWarpTarget(AttackWarpTargetName);
	}
}

void ABaseCharacter::UpdateAttackWarping()
{
	ACharacter* Target = AttackWarpTargetActor.Get();
	if (!IsAttackWarpTargetUsable(Target) || !MotionWarpingComponent)
	{
		EndAttackWarping();
		return;
	}

	FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
	ToTarget.Z = 0.f;
	const float DistanceToTarget = ToTarget.Size();
	const FVector DirectionToTarget = DistanceToTarget > UE_KINDA_SMALL_NUMBER
		? ToTarget / DistanceToTarget
		: GetActorForwardVector().GetSafeNormal2D();

	const float DesiredYaw = DistanceToTarget > UE_KINDA_SMALL_NUMBER
		? DirectionToTarget.Rotation().Yaw
		: GetActorRotation().Yaw;
	const float DesiredYawFromAttackStart = FMath::FindDeltaAngleDegrees(AttackFacingOriginYaw, DesiredYaw);
	const float ClampedDesiredYaw = AttackFacingOriginYaw + FMath::Clamp(
		DesiredYawFromAttackStart,
		-AttackMaxFacingAngle,
		AttackMaxFacingAngle);
	const FRotator DesiredRotation(0.f, FRotator::NormalizeAxis(ClampedDesiredYaw), 0.f);

	const UCapsuleComponent* AttackerCapsule = GetCapsuleComponent();
	const UCapsuleComponent* TargetCapsule = Target->GetCapsuleComponent();
	const float AttackerRadius = AttackerCapsule ? AttackerCapsule->GetScaledCapsuleRadius() : 0.f;
	const float TargetRadius = TargetCapsule ? TargetCapsule->GetScaledCapsuleRadius() : 0.f;
	const float DesiredCenterDistance = AttackerRadius + TargetRadius + AttackWarpSurfaceGap;

	const bool bWithinMaxWarpDistance =
		AttackWarpMaxDistance > 0.f &&
		DistanceToTarget <= AttackWarpMaxDistance;
	const bool bCanWarpPosition = AttackWarpMaxTravelDistance > 0.f &&
		(bWithinMaxWarpDistance ||
			bKeepAttackWarpTargetWhenBeyondMaxDistance);
	if (bCanWarpPosition)
	{
		// 终点停在双方胶囊表面之外；已经过近时 TravelDistance 为 0，不会反向拉走角色。
		const float RequiredTravelDistance = FMath::Max(0.f, DistanceToTarget - DesiredCenterDistance);
		const float TravelDistance = FMath::Min(
			RequiredTravelDistance,
			AttackWarpMaxTravelDistance);
		FVector WarpLocation = GetActorLocation() + DirectionToTarget * TravelDistance;
		// 近战吸附只处理水平面；上下坡和台阶继续交给 CharacterMovement/原始根运动。
		WarpLocation.Z = GetActorLocation().Z;

		MotionWarpingComponent->AddOrUpdateWarpTargetFromTransform(
			AttackWarpTargetName,
			FTransform(DesiredRotation, WarpLocation));
	}
	else
	{
		// 超距时必须立即删除，而不是让当前或下一次 Montage 继续读取旧位置。
		MotionWarpingComponent->RemoveWarpTarget(AttackWarpTargetName);
	}

	const URootMotionModifier_Warp* ActiveWarpModifier = FindActiveAttackWarpModifier();
	if (!ActiveWarpModifier)
	{
		return;
	}

	// 有目标时优先让 Motion Warping 自己处理旋转；翻译关闭或超距无目标时才手动平滑修正。
	// 这样不会再由计时器和根运动同时争夺角色 Rotation。
	if (!bCanWarpPosition || !ActiveWarpModifier->bWarpRotation)
	{
		FRotator NewRotation = GetActorRotation();
		const float DeltaSeconds = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.02f;
		NewRotation.Yaw = FMath::FixedTurn(
			NewRotation.Yaw,
			DesiredRotation.Yaw,
			AttackFacingTurnRate * FMath::Max(DeltaSeconds, 0.f));
		SetActorRotation(NewRotation);
	}
}

const URootMotionModifier_Warp* ABaseCharacter::FindActiveAttackWarpModifier() const
{
	if (!MotionWarpingComponent)
	{
		return nullptr;
	}

	for (const URootMotionModifier* Modifier : MotionWarpingComponent->GetModifiers())
	{
		const URootMotionModifier_Warp* WarpModifier = Cast<URootMotionModifier_Warp>(Modifier);
		if (WarpModifier &&
			WarpModifier->WarpTargetName == AttackWarpTargetName &&
			WarpModifier->GetState() == ERootMotionModifierState::Active)
		{
			return WarpModifier;
		}
	}

	return nullptr;
}

bool ABaseCharacter::IsAttackWarpTargetUsable(const ACharacter* Target) const
{
	if (!IsValid(Target) || Target == this || Target->IsActorBeingDestroyed())
	{
		return false;
	}

	const UAbilitySystemComponent* TargetASC =
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<ACharacter*>(Target));
	return !TargetASC || !TargetASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead);
}

UAbilitySystemComponent* ABaseCharacter::GetAbilitySystemComponent() const
{
	return FirstAbilitySystemComponent;
}

UPawnUIComponent* ABaseCharacter::GetPawnUIComponent() const
{
	return nullptr;
}

void ABaseCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	
	// 单机/服务器通常走这里。完成 ActorInfo 后才允许读取 StartUpData。
	InitializeAbilitySystem();
	GiveStartupData();
}

void ABaseCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	
	// 当前作业把 ASC 放在 Character 上，单机时这个回调通常不是关键路径；
	// 仍保留它，让以后把 ASC 迁移到 PlayerState 时有清晰的客户端初始化入口。
	InitializeAbilitySystem();
	GiveStartupData();
}


// Called to bind functionality to input
void ABaseCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

}

void ABaseCharacter::InitializeAbilitySystem()
{
	if (!FirstAbilitySystemComponent)
	{
		return;
	}
	
	// 当前作业 ASC 放在 Character 上，OwnerActor 和 AvatarActor 都使用 this。
	// 以后如果迁移到 PlayerState，再调整 OwnerActor。
	FirstAbilitySystemComponent->InitAbilityActorInfo(this, this);
	
	UE_LOG(LogTemp, Warning, TEXT("[%s] ASC initialized"), *GetNameSafe(this));
}

void ABaseCharacter::GiveStartupData()
{
	if (bStartupDataGiven || CharacterStartUpData.IsNull() || !FirstAbilitySystemComponent)
	{
		return;
	}
	
	if (UDataAsset_StartUpDataBase* LoadedData = CharacterStartUpData.LoadSynchronous())
	{
		// TSoftObjectPtr 允许蓝图引用数据资产但避免构造阶段强加载；
		// 角色真正被控制后才同步加载一次，学习项目中更易理解和验证。
		LoadedData->GiveToAbilitySystemComponent(FirstAbilitySystemComponent);
		bStartupDataGiven = true;
	}
}

FVector ABaseCharacter::GetDodgeInputDirection() const
{
	FVector Direction = GetActorForwardVector();
	Direction.Z = 0.0f;
	Direction.Normalize();
	return Direction;
}

float ABaseCharacter::GetDodgeReferenceYaw() const
{
	if (Controller)
	{
		return Controller->GetControlRotation().Yaw;
	}
	
	return GetActorRotation().Yaw;
}

int32 ABaseCharacter::GetDodgeDirectionIndex() const
{
	return QuantizeDodgeDirection(GetDodgeInputDirection(), GetDodgeReferenceYaw());
}

int32 ABaseCharacter::QuantizeDodgeDirection(const FVector& InDirection, float ReferenceYaw)
{
	FVector Direction = InDirection;
	Direction.Z = 0.0f;
	
	if (Direction.IsNearlyZero())
	{
		return 0;// 前
	}
	
	Direction.Normalize();
	
	const float DirectionYaw = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
	const float RelativeYaw = FRotator::NormalizeAxis(DirectionYaw - ReferenceYaw);
	
	// 每 45° 一格，映射到 0～7。
	int32 Index = FMath::RoundToInt(RelativeYaw / 45.f);
	Index = (Index + 8) % 8;
	
	return Index;
	
}

UAnimMontage* ABaseCharacter::GetDodgeMontageForDirection(int32 DirectionIndex) const
{
	if (DodgeMontages.IsValidIndex(DirectionIndex) && DodgeMontages[DirectionIndex])
	{
		return DodgeMontages[DirectionIndex].Get();
	}
	
	return DodgeMontage;
}

