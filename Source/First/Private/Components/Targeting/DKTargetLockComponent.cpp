#include "Components/Targeting/DKTargetLockComponent.h"

#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Character/BaseCharacter.h"
#include "Character/DKCharacter.h"
#include "Character/EnemyCharacter.h"
#include "CollisionQueryParams.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "MyGameplayTags.h"

UDKTargetLockComponent::UDKTargetLockComponent()
{
	// 组件拥有自己的 Tick，不需要重新开启 ADKCharacter 的 Actor Tick。
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UDKTargetLockComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerCharacter = Cast<ADKCharacter>(GetOwner());
	SetComponentTickEnabled(false);

	if (!OwnerCharacter)
	{
		UE_LOG(LogTemp,Error,TEXT("[TargetLock] 组件 Owner 不是 ADKCharacter"));
	}
}

void UDKTargetLockComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearTargetLockInternal(TEXT("EndPlay"));

	if (TargetIndicatorWidget)
	{
		TargetIndicatorWidget->RemoveFromParent();
		TargetIndicatorWidget = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void UDKTargetLockComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime,TickType,ThisTickFunction);

	UpdateLockedTarget(DeltaTime);
}

void UDKTargetLockComponent::ToggleTargetLock()
{
	if (bLockModeActive)
	{
		ClearTargetLockInternal(TEXT("Manual"));
		return;
	}

	if (!OwnerCharacter ||
		IsCharacterDead(OwnerCharacter))
	{
		return;
	}

	AEnemyCharacter* BestTarget = FindBestTarget();
	if (!BestTarget)
	{
		UE_LOG(LogTemp,Log,TEXT("[TargetLock] Acquire failed: no valid target"));
		return;
	}

	SetCurrentTarget(BestTarget);
}

TArray<AEnemyCharacter*>
UDKTargetLockComponent::GatherCandidates(
	bool bRequireAcquireCone) const
{
	TArray<AEnemyCharacter*> Result;

	if (!OwnerCharacter || !GetWorld())
	{
		return Result;
	}

	// 先按 Pawn 对象类型做球形查询，再用 AEnemyCharacter 做语义过滤。
	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));

	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(OwnerCharacter);

	TArray<AActor*> OverlappedActors;
	UKismetSystemLibrary::SphereOverlapActors(
		this,
		OwnerCharacter->GetActorLocation(),
		AcquireRadius,
		ObjectTypes,
		AEnemyCharacter::StaticClass(),
		ActorsToIgnore,
		OverlappedActors);

	for (AActor* Actor : OverlappedActors)
	{
		AEnemyCharacter* Candidate =Cast<AEnemyCharacter>(Actor);

		if (IsCandidateValid(Candidate,bRequireAcquireCone))
		{
			Result.AddUnique(Candidate);
		}
	}

	return Result;
}

bool UDKTargetLockComponent::IsCandidateValid(AEnemyCharacter* Candidate,bool bRequireAcquireCone) const
{
	if (!OwnerCharacter ||!IsValid(Candidate) ||IsCharacterDead(Candidate))
	{
		return false;
	}

	const FVector OwnerLocation =OwnerCharacter->GetActorLocation();
	const FVector TargetLocation =Candidate->GetTargetLockLocation();
	const FVector OwnerToTarget =TargetLocation - OwnerLocation;

	if (OwnerToTarget.SizeSquared() >FMath::Square(AcquireRadius))
	{
		return false;
	}

	if (FMath::Abs(Candidate->GetActorLocation().Z -OwnerLocation.Z) > MaxHeightDifference)
	{
		return false;
	}

	APlayerController* PlayerController =Cast<APlayerController>(OwnerCharacter->GetController());
	if (!PlayerController)
	{
		return false;
	}

	if (bRequireAcquireCone)
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		PlayerController->GetPlayerViewPoint(ViewLocation,ViewRotation);

		FVector FlatViewForward = ViewRotation.Vector();
		FlatViewForward.Z = 0.f;
		FlatViewForward.Normalize();

		FVector FlatViewToTarget =TargetLocation - ViewLocation;
		FlatViewToTarget.Z = 0.f;
		FlatViewToTarget.Normalize();

		const float MinimumDot =FMath::Cos(FMath::DegreesToRadians(MaxAcquireAngleDegrees));

		if (FVector::DotProduct(FlatViewForward,FlatViewToTarget) < MinimumDot)
		{
			return false;
		}
	}

	return HasLineOfSightTo(Candidate);
}

bool UDKTargetLockComponent::IsCharacterDead(const ABaseCharacter* Character) const
{
	if (!Character)
	{
		return true;
	}

	const UFirstAbilitySystemComponent* ASC =Character->GetFirstAbilitySystemComponent();

	return ASC &&ASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead);
}

bool UDKTargetLockComponent::HasLineOfSightTo(
	AEnemyCharacter* Candidate) const
{
	if (!OwnerCharacter ||!Candidate ||!GetWorld())
	{
		return false;
	}

	APlayerController* PlayerController =Cast<APlayerController>(OwnerCharacter->GetController());
	if (!PlayerController)
	{
		return false;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerController->GetPlayerViewPoint(ViewLocation,ViewRotation);

	FHitResult HitResult;
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(TargetLockVisibility),false,OwnerCharacter);
	TraceParams.AddIgnoredActor(OwnerCharacter);

	const bool bHitSomething =GetWorld()->LineTraceSingleByChannel(
			HitResult,
			ViewLocation,
			Candidate->GetTargetLockLocation(),
			ECC_Visibility,
			TraceParams);

	if (!bHitSomething)
	{
		return true;
	}

	AActor* HitActor = HitResult.GetActor();
	return HitActor == Candidate ||(HitActor && HitActor->IsOwnedBy(Candidate));
}

bool UDKTargetLockComponent::ProjectTargetToScreen(AEnemyCharacter* Candidate,FVector2D& OutScreenPosition) const
{
	if (!OwnerCharacter || !Candidate)
	{
		return false;
	}

	APlayerController* PlayerController =Cast<APlayerController>(OwnerCharacter->GetController());
	if (!PlayerController)
	{
		return false;
	}

	const bool bProjected =PlayerController->ProjectWorldLocationToScreen(
			Candidate->GetTargetLockLocation(),
			OutScreenPosition,
			false);

	int32 ViewportX = 0;
	int32 ViewportY = 0;
	PlayerController->GetViewportSize(ViewportX,ViewportY);

	return bProjected &&ViewportX > 0 &&ViewportY > 0 &&OutScreenPosition.X >= 0.f &&OutScreenPosition.X <= ViewportX &&OutScreenPosition.Y >= 0.f &&OutScreenPosition.Y <= ViewportY;
}

float UDKTargetLockComponent::CalculateAcquireScore(AEnemyCharacter* Candidate) const
{
	FVector2D ScreenPosition;
	if (!ProjectTargetToScreen(Candidate,ScreenPosition))
	{
		return MAX_flt;
	}

	APlayerController* PlayerController =Cast<APlayerController>(OwnerCharacter->GetController());

	int32 ViewportX = 0;
	int32 ViewportY = 0;
	PlayerController->GetViewportSize(ViewportX,ViewportY);

	const FVector2D ViewportCenter(ViewportX * 0.5f,ViewportY * 0.5f);

	const float HalfDiagonal =FMath::Max(ViewportCenter.Size(),1.f);

	const float ScreenCenterScore =(ScreenPosition - ViewportCenter).Size() /HalfDiagonal;

	const float DistanceScore =FVector::Dist(OwnerCharacter->GetActorLocation(),Candidate->GetActorLocation()) /FMath::Max(AcquireRadius, 1.f);

	// 屏幕中心优先，其次才是距离。
	return ScreenCenterScore * 0.75f +DistanceScore * 0.25f;
}

AEnemyCharacter*
UDKTargetLockComponent::FindBestTarget() const
{
	const TArray<AEnemyCharacter*> Candidates =GatherCandidates(true);

	AEnemyCharacter* BestTarget = nullptr;
	float BestScore = MAX_flt;

	for (AEnemyCharacter* Candidate : Candidates)
	{
		const float Score =CalculateAcquireScore(Candidate);

		if (Score < BestScore)
		{
			BestScore = Score;
			BestTarget = Candidate;
		}
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[TargetLock] Acquire Candidates=%d Chosen=%s Score=%.3f"),
		Candidates.Num(),
		*GetNameSafe(BestTarget),
		BestScore);

	return BestTarget;
}

void UDKTargetLockComponent::SetCurrentTarget(AEnemyCharacter* NewTarget)
{
	if (!OwnerCharacter || !IsValid(NewTarget))
	{
		return;
	}

	const bool bWasAlreadyLocked = bLockModeActive;

	CurrentTarget = NewTarget;
	OccludedElapsedTime = 0.f;

	if (!bWasAlreadyLocked)
	{
		bLockModeActive = true;
		ApplyLockedMovementMode();
		ShowTargetIndicator();
		SetComponentTickEnabled(true);

		if (UFirstAbilitySystemComponent* ASC =OwnerCharacter->GetFirstAbilitySystemComponent())
		{
			ASC->AddLooseGameplayTag(MyGameplayTags::DK_Status_TargetLocked);
			bAddedTargetLockTag = true;
		}
	}

	UpdateTargetIndicator();
	OnTargetLockChanged.Broadcast(NewTarget);

	UE_LOG(LogTemp,Log,TEXT("[TargetLock] Locked: %s"),*GetNameSafe(NewTarget));
}

void UDKTargetLockComponent::SwitchTarget(float Direction)
{
	if (!IsTargetLocked() ||FMath::Abs(Direction) < 0.5f ||!GetWorld())
	{
		return;
	}

	const float CurrentTime =GetWorld()->GetTimeSeconds();
	if (CurrentTime < NextAllowedSwitchTime)
	{
		return;
	}
	NextAllowedSwitchTime =CurrentTime + SwitchCooldown;

	AEnemyCharacter* OldTarget =CurrentTarget.Get();

	FVector2D OldTargetScreenPosition;
	if (!ProjectTargetToScreen(OldTarget,OldTargetScreenPosition))
	{
		return;
	}

	APlayerController* PlayerController =Cast<APlayerController>(OwnerCharacter->GetController());

	int32 ViewportX = 0;
	int32 ViewportY = 0;
	PlayerController->GetViewportSize(ViewportX,ViewportY);

	const TArray<AEnemyCharacter*> Candidates =GatherCandidates(false);

	AEnemyCharacter* BestTarget = nullptr;
	float BestScore = MAX_flt;

	for (AEnemyCharacter* Candidate : Candidates)
	{
		if (Candidate == OldTarget)
		{
			continue;
		}

		FVector2D CandidateScreenPosition;
		if (!ProjectTargetToScreen(Candidate,CandidateScreenPosition))
		{
			continue;
		}

		const FVector2D ScreenDelta =CandidateScreenPosition -OldTargetScreenPosition;

		if (Direction > 0.f &&ScreenDelta.X <= SwitchScreenDeadZone)
		{
			continue;
		}

		if (Direction < 0.f &&ScreenDelta.X >= -SwitchScreenDeadZone)
		{
			continue;
		}

		const float NormalizedX =FMath::Abs(ScreenDelta.X) /FMath::Max(static_cast<float>(ViewportX),1.f);

		const float NormalizedY =FMath::Abs(ScreenDelta.Y) /FMath::Max(static_cast<float>(ViewportY),1.f);

		const float DistanceScore =FVector::Dist(OwnerCharacter->GetActorLocation(),Candidate->GetActorLocation()) /FMath::Max(AcquireRadius, 1.f);

		const float Score =NormalizedX +NormalizedY * 1.5f +DistanceScore * 0.15f;

		if (Score < BestScore)
		{
			BestScore = Score;
			BestTarget = Candidate;
		}
	}

	if (!BestTarget)
	{
		// 同侧没有候选：保持当前目标，不自动解除。
		return;
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[TargetLock] Switch Dir=%.1f From=%s To=%s"),
		Direction,
		*GetNameSafe(OldTarget),
		*GetNameSafe(BestTarget));

	SetCurrentTarget(BestTarget);
}

void UDKTargetLockComponent::UpdateLockedTarget(float DeltaTime)
{
	if (!bLockModeActive)
	{
		return;
	}

	if (!OwnerCharacter)
	{
		ClearTargetLockInternal(TEXT("InvalidOwner"));
		return;
	}

	if (IsCharacterDead(OwnerCharacter))
	{
		ClearTargetLockInternal(TEXT("OwnerDead"));
		return;
	}

	if (!OwnerCharacter->GetController())
	{
		ClearTargetLockInternal(TEXT("NoController"));
		return;
	}

	AEnemyCharacter* Target = CurrentTarget.Get();
	if (!IsValid(Target))
	{
		ClearTargetLockInternal(TEXT("Destroyed"));
		return;
	}

	if (IsCharacterDead(Target))
	{
		ClearTargetLockInternal(TEXT("Dead"));
		return;
	}

	const FVector OwnerLocation =OwnerCharacter->GetActorLocation();

	if (FVector::DistSquared(OwnerLocation,Target->GetActorLocation()) >FMath::Square(BreakDistance))
	{
		ClearTargetLockInternal(TEXT("Distance"));
		return;
	}

	if (FMath::Abs(Target->GetActorLocation().Z -OwnerLocation.Z) > MaxHeightDifference)
	{
		ClearTargetLockInternal(TEXT("Height"));
		return;
	}

	if (HasLineOfSightTo(Target))
	{
		OccludedElapsedTime = 0.f;
	}
	else
	{
		OccludedElapsedTime += DeltaTime;

		if (OccludedElapsedTime >=OcclusionGraceTime)
		{
			ClearTargetLockInternal(TEXT("Occluded"));
			return;
		}
	}

	UpdateControlRotation(DeltaTime);
	UpdateTargetIndicator();
}

void UDKTargetLockComponent::UpdateControlRotation(
	float DeltaTime)
{
	AEnemyCharacter* Target = CurrentTarget.Get();
	APlayerController* PlayerController =OwnerCharacter? Cast<APlayerController>(OwnerCharacter->GetController()): nullptr;

	if (!Target || !PlayerController)
	{
		return;
	}

	FVector ViewLocation;
	FRotator UnusedViewRotation;
	PlayerController->GetPlayerViewPoint(ViewLocation,UnusedViewRotation);

	FRotator DesiredRotation =(Target->GetTargetLockLocation() -ViewLocation).Rotation();

	DesiredRotation.Pitch = FMath::Clamp(FRotator::NormalizeAxis(DesiredRotation.Pitch),MinCameraPitch,MaxCameraPitch);
	DesiredRotation.Roll = 0.f;

	FRotator NewRotation = FMath::RInterpTo(PlayerController->GetControlRotation(),DesiredRotation,DeltaTime,CameraRotationInterpSpeed);
	NewRotation.Roll = 0.f;

	PlayerController->SetControlRotation(NewRotation);
}

void UDKTargetLockComponent::ApplyLockedMovementMode()
{
	if (!OwnerCharacter)
	{
		return;
	}

	UCharacterMovementComponent* Movement =OwnerCharacter->GetCharacterMovement();
	if (!Movement)
	{
		return;
	}

	if (!bMovementSettingsCached)
	{
		bCachedOrientRotationToMovement =Movement->bOrientRotationToMovement;
		bCachedUseControllerDesiredRotation =Movement->bUseControllerDesiredRotation;
		bMovementSettingsCached = true;
	}

	// 角色不再朝移动方向转，而是平滑朝 Controller 的目标 Yaw。
	Movement->bOrientRotationToMovement = false;
	Movement->bUseControllerDesiredRotation = true;
}

void UDKTargetLockComponent::RestoreMovementMode()
{
	if (!OwnerCharacter ||!bMovementSettingsCached)
	{
		return;
	}

	if (UCharacterMovementComponent* Movement =OwnerCharacter->GetCharacterMovement())
	{
		Movement->bOrientRotationToMovement =bCachedOrientRotationToMovement;
		Movement->bUseControllerDesiredRotation =bCachedUseControllerDesiredRotation;
	}

	bMovementSettingsCached = false;
}

void UDKTargetLockComponent::ShowTargetIndicator()
{
	if (!OwnerCharacter ||!TargetIndicatorWidgetClass)
	{
		// 没配置 UI 不应让核心锁敌失败。
		return;
	}

	APlayerController* PlayerController =Cast<APlayerController>(OwnerCharacter->GetController());
	if (!PlayerController)
	{
		return;
	}

	if (!TargetIndicatorWidget)
	{
		TargetIndicatorWidget =CreateWidget<UUserWidget>(PlayerController,TargetIndicatorWidgetClass);

		if (!TargetIndicatorWidget)
		{
			return;
		}

		TargetIndicatorWidget->AddToViewport(20);
		TargetIndicatorWidget->SetAlignmentInViewport(FVector2D(0.5f, 0.5f));
		TargetIndicatorWidget->SetDesiredSizeInViewport(TargetIndicatorSize);
	}

		TargetIndicatorWidget->SetVisibility(
		ESlateVisibility::HitTestInvisible);
}

void UDKTargetLockComponent::HideTargetIndicator()
{
	if (TargetIndicatorWidget)
	{
		TargetIndicatorWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UDKTargetLockComponent::UpdateTargetIndicator()
{
	if (!TargetIndicatorWidget ||!CurrentTarget.IsValid() ||!OwnerCharacter)
	{
		return;
	}

	APlayerController* PlayerController =Cast<APlayerController>(OwnerCharacter->GetController());
	if (!PlayerController)
	{
		return;
	}

	FVector2D WidgetPosition;
	const bool bProjected =
		UWidgetLayoutLibrary::
			ProjectWorldLocationToWidgetPosition(
				PlayerController,
				GetCurrentTargetLocation(),
				WidgetPosition,
				false);

	if (!bProjected)
	{
		TargetIndicatorWidget->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	TargetIndicatorWidget->SetVisibility(ESlateVisibility::HitTestInvisible);

	// ProjectWorldLocationToWidgetPosition 已处理 DPI；
	// 因此 SetPositionInViewport 的 RemoveDPIScale 使用 false。
	TargetIndicatorWidget->SetPositionInViewport(WidgetPosition,false);
}

void UDKTargetLockComponent::ClearTargetLock()
{
	ClearTargetLockInternal(TEXT("Manual"));
}

void UDKTargetLockComponent::ClearTargetLockInternal(
	const TCHAR* Reason)
{
	if (!bLockModeActive &&!CurrentTarget.IsValid())
	{
		return;
	}

	const FString OldTargetName =GetNameSafe(CurrentTarget.Get());

	CurrentTarget.Reset();
	OccludedElapsedTime = 0.f;
	NextAllowedSwitchTime = 0.f;

	SetComponentTickEnabled(false);
	RestoreMovementMode();
	HideTargetIndicator();

	if (bAddedTargetLockTag && OwnerCharacter)
	{
		if (UFirstAbilitySystemComponent* ASC =OwnerCharacter->GetFirstAbilitySystemComponent())
		{
			ASC->RemoveLooseGameplayTag(MyGameplayTags::DK_Status_TargetLocked);
		}
	}

	bAddedTargetLockTag = false;
	bLockModeActive = false;

	OnTargetLockChanged.Broadcast(nullptr);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[TargetLock] Cleared Reason=%s Old=%s"),
		Reason,
		*OldTargetName);
}

FVector UDKTargetLockComponent::
GetCurrentTargetLocation() const
{
	const AEnemyCharacter* Target =CurrentTarget.Get();

	return Target? Target->GetTargetLockLocation(): FVector::ZeroVector;
}