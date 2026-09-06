// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/BossCharacter.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "MyGameplayTags.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "AbilitySystem/FirstAttributeSet.h"
#include "AIController.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Components/UI/EnemyUIComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/Combat/BossCombatComponent.h"
#include "Items/Weapons/BossWeapon.h"
#include "MyGameplayTags.h"
#include "AbilitySystem/GameplayEffects/FirstGE_PoiseChange.h"
#include "NavigationSystem.h"

ABossCharacter::ABossCharacter()
{
	// 巡逻与追击双速度：默认步行（250），追击时由行为树服务切到奔跑（500）。
	GetCharacterMovement()->MaxWalkSpeed = PatrolMoveSpeed;

	// 旋转交给三态旋转模式统一管理：默认朝移动方向（巡逻/远追）。
	bUseControllerRotationYaw = false;
	SetBossRotationMode(EBossRotationMode::OrientToMovement);
	
	EnemyUIComponent = CreateDefaultSubobject<UEnemyUIComponent>(TEXT("EnemyUIComponent"));
	BossCombatComponent = CreateDefaultSubobject<UBossCombatComponent>(TEXT("BossCombatComponent"));
	ExecutionPoint =CreateDefaultSubobject<USceneComponent>(TEXT("ExecutionPoint"));
	ExecutionPoint->SetupAttachment(GetRootComponent());

	// 默认在 BOSS 正前方 120 单位，Yaw 180° 表示玩家面向 BOSS。
	ExecutionPoint->SetRelativeLocation(FVector(120.f, 0.f, 0.f));
	ExecutionPoint->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
	
	NormalAttackDefenseData.bBlockable = true;
	NormalAttackDefenseData.bParryable = true;
	NormalAttackDefenseData.GuardStaminaDamage = 25.f;
	NormalAttackDefenseData.ParryPoiseDamage = 50.f;

	ThreeComboDefenseData.bBlockable = true;
	ThreeComboDefenseData.bParryable = true;
	ThreeComboDefenseData.GuardStaminaDamage = 15.f;
	ThreeComboDefenseData.ParryPoiseDamage = 50.f;

	// 五连斩（霸体招式）：弹反照常削韧（ParryStagger 霸体分支），
	// 防御参数首版对齐三连斩。
	FiveComboDefenseData.bBlockable = true;
	FiveComboDefenseData.bParryable = true;
	FiveComboDefenseData.GuardStaminaDamage = 15.f;
	FiveComboDefenseData.ParryPoiseDamage = 50.f;

	RetreatChargedSlashDefenseData.bBlockable = true;
	RetreatChargedSlashDefenseData.bParryable = true;
	RetreatChargedSlashDefenseData.GuardStaminaDamage = 40.f;
	RetreatChargedSlashDefenseData.ParryPoiseDamage = 50.f;
}

UPawnUIComponent* ABossCharacter::GetPawnUIComponent() const
{
	return EnemyUIComponent;
}

FTransform ABossCharacter::GetExecutionPointTransform() const
{
	return ExecutionPoint? ExecutionPoint->GetComponentTransform(): GetActorTransform();
}

bool ABossCharacter::HasSafeRetreatSpace(
	float RetreatDistance,
	float NavProjectionTolerance,
	float MaxLandingHeightDelta) const
{
	if (!FMath::IsFinite(RetreatDistance) ||
		!FMath::IsFinite(NavProjectionTolerance) ||
		!FMath::IsFinite(MaxLandingHeightDelta) ||
		RetreatDistance < 0.f ||
		NavProjectionTolerance < 0.f ||
		MaxLandingHeightDelta < 0.f)
	{
		return false;
	}

	if (RetreatDistance <= KINDA_SMALL_NUMBER)
	{
		return true;
	}

	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	UWorld* World = GetWorld();
	const AAIController* AIController = Cast<AAIController>(GetController());
	if (!Capsule || !World || !AIController)
	{
		return false;
	}

	FVector RetreatDirection = -GetActorForwardVector();
	RetreatDirection.Z = 0.f;
	if (!RetreatDirection.Normalize())
	{
		return false;
	}

	// Sweep 使用胶囊中心；导航落点使用角色脚底，避免把胶囊半高误判成落差。
	const FVector SweepStart = Capsule->GetComponentLocation();
	const FVector SweepEnd = SweepStart + RetreatDirection * RetreatDistance;
	FComponentQueryParams QueryParams(
		SCENE_QUERY_STAT(BossSafeRetreatSweep),
		this);
	QueryParams.AddIgnoredActor(this);

	// CollisionProfileName == "Custom" is UE's reserved sentinel, not a named
	// profile that can be passed to SweepSingleByProfile. Sweep the capsule
	// component itself so the test uses its real geometry, object channel and
	// per-channel responses without producing a missing-profile warning.
	TArray<FHitResult> BlockingHits;
	if (World->ComponentSweepMulti(
		BlockingHits,
		const_cast<UCapsuleComponent*>(Capsule),
		SweepStart,
		SweepEnd,
		Capsule->GetComponentQuat(),
		QueryParams))
	{
		return false;
	}

	UNavigationSystemV1* NavSystem =
		UNavigationSystemV1::GetCurrent(World);
	if (!NavSystem)
	{
		return false;
	}

	const FVector DesiredLanding =
		GetNavAgentLocation() + RetreatDirection * RetreatDistance;
	const FVector ProjectionExtent(
		FMath::Max(NavProjectionTolerance, 1.f),
		FMath::Max(NavProjectionTolerance, 1.f),
		FMath::Max(MaxLandingHeightDelta, 1.f));
	FNavLocation ProjectedLanding;
	const FNavAgentProperties& NavAgentProperties =
		AIController->GetNavAgentPropertiesRef();

	if (!NavSystem->ProjectPointToNavigation(
		DesiredLanding,
		ProjectedLanding,
		ProjectionExtent,
		&NavAgentProperties))
	{
		return false;
	}

	const float LandingOffset2D = FVector::Dist2D(
		DesiredLanding,
		ProjectedLanding.Location);
	const float LandingHeightDelta = FMath::Abs(
		DesiredLanding.Z - ProjectedLanding.Location.Z);

	if (LandingOffset2D > NavProjectionTolerance ||
		LandingHeightDelta > MaxLandingHeightDelta)
	{
		return false;
	}

	// 根运动会沿直线后撤，不能只验证终点：中间若跨过窄坑或断开的 NavMesh，
	// 终点仍可能投影成功。导航射线使用同一个 AIController/NavAgent 检查整段可行走性。
	FVector NavigationHitLocation = DesiredLanding;
	return !UNavigationSystemV1::NavigationRaycast(
		GetWorld(),
		GetNavAgentLocation(),
		ProjectedLanding.Location,
		NavigationHitLocation,
		nullptr,
		const_cast<AAIController*>(AIController));
}

void ABossCharacter::SetBossRotationMode(EBossRotationMode NewMode)
{
	BossRotationMode = NewMode;

	// 始终让 CharacterMovement 按 RotationRate 平滑旋转。
	bUseControllerRotationYaw = false;

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		// 先关闭两套自动转向，再只开启当前模式需要的一套。
		Movement->bOrientRotationToMovement = false;
		Movement->bUseControllerDesiredRotation = false;

		switch (NewMode)
		{
		case EBossRotationMode::OrientToMovement:
			Movement->bOrientRotationToMovement = true;
			break;

		case EBossRotationMode::FaceTarget:
			Movement->bUseControllerDesiredRotation = true;
			break;

		case EBossRotationMode::Frozen:
		default:
			// 两者保持 false：CharacterMovement 不再自动改变 Yaw。
			break;
		}
	}
}

EFirstPoiseDamageResult ABossCharacter::ApplyPoiseDamage(float Damage, AActor* SourceActor)
{
	UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponent();

	if (!ASC || !FirstAttributeSet ||
		!FMath::IsFinite(Damage) ||
		Damage <= KINDA_SMALL_NUMBER)
	{
		return EFirstPoiseDamageResult::Ignored;
	}

	// 已经进入终局状态时，不再修改 Poise，也不刷新可处决窗口。
	if (ASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead) ||
		ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Executable) ||
		ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_BeingExecuted) ||
		ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Executed))
	{
		return EFirstPoiseDamageResult::Ignored;
	}

	const float OldPoise = FirstAttributeSet->GetPoise();
	if (!FMath::IsFinite(OldPoise) ||OldPoise <= KINDA_SMALL_NUMBER)
	{
		return EFirstPoiseDamageResult::Ignored;
	}

	// 外部传正数，只有这里负责转换成 GE 使用的负 Delta。
	ApplyPoiseDeltaRaw(-Damage, SourceActor);

	const float NewPoise = FirstAttributeSet->GetPoise();
	if (!FMath::IsFinite(NewPoise) ||
		NewPoise >= OldPoise)
	{
		// Spec 无效、GE 未执行或数值没有实际下降时，不启动计时器。
		return EFirstPoiseDamageResult::Ignored;
	}

	// 每次“实际下降”都从头计时：未破韧走 PoiseRecoveryDelay（韧性回复时间），
	// 破韧这一击切换为 ExecutableWindowDuration（可处决窗口），两个时长独立配置。
	if (NewPoise > KINDA_SMALL_NUMBER)
	{
		RestartPoiseRecoveryTimer();
		return EFirstPoiseDamageResult::Reduced;
	}

	RestartExecutableWindowTimer();

	// OldPoise > 0 且 NewPoise == 0：天然的一次性归零边沿。
	// Poise 已为 0 的后续命中会在前面被忽略，因此不会重复发送。
	FGameplayEventData PoiseBrokenEvent;
	PoiseBrokenEvent.Instigator = IsValid(SourceActor)? SourceActor: this;
	PoiseBrokenEvent.Target = this;
	PoiseBrokenEvent.EventMagnitude = OldPoise - NewPoise;

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(this,MyGameplayTags::Boss_Event_PoiseBroken,PoiseBrokenEvent);

	return EFirstPoiseDamageResult::Broken;
}

void ABossCharacter::ApplyPoiseDeltaRaw(float Delta,AActor* SourceActor)
{
	UFirstAbilitySystemComponent* ASC =GetFirstAbilitySystemComponent();
	if (!ASC || FMath::IsNearlyZero(Delta))
	{
		return;
	}

	// SourceActor 只用于正确记录本次削韧来自谁。
	// 回满时传 this；普通武器/弹反时传玩家。
	AActor* EffectiveSource = IsValid(SourceActor)? SourceActor: this;

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(EffectiveSource);
	Context.AddInstigator(EffectiveSource, EffectiveSource);

	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UFirstGE_PoiseChange::StaticClass(),1.f,Context);

	if (!Spec.IsValid())
	{
		return;
	}

	Spec.Data->SetSetByCallerMagnitude(MyGameplayTags::Combat_SetByCaller_PoiseDelta,Delta);

	ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
}


void ABossCharacter::RestartPoiseRecoveryTimer()
{
	RestartPoiseTimerInternal(PoiseRecoveryDelay);
}

void ABossCharacter::RestartExecutableWindowTimer()
{
	RestartPoiseTimerInternal(ExecutableWindowDuration);
}

void ABossCharacter::RestartPoiseTimerInternal(float Delay)
{
	GetWorldTimerManager().ClearTimer(PoiseRecoveryTimerHandle);

	// 不能在 ApplyPoiseDamage 调用栈中同步回满。
	const float SafeDelay = FMath::Max(Delay, 0.1f);

	GetWorldTimerManager().SetTimer(PoiseRecoveryTimerHandle,this,&ThisClass::HandlePoiseRecoveryExpired,SafeDelay,false);
}

void ABossCharacter::ClearPoiseRecoveryTimer()
{
	GetWorldTimerManager().ClearTimer(PoiseRecoveryTimerHandle);
}

void ABossCharacter::RestorePoiseToFull()
{
	// 正常到期、异常取消或手动恢复都先清理旧 Timer，
	// 防止已经回满后残留回调再次执行。
	ClearPoiseRecoveryTimer();

	if (!FirstAttributeSet)
	{
		return;
	}

	const float MissingPoise =FirstAttributeSet->GetMaxPoise() -FirstAttributeSet->GetPoise();

	if (MissingPoise > 0.f)
	{
		ApplyPoiseDeltaRaw(MissingPoise, this);
	}
}

void ABossCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (bUseSpawnLocationAsHome)
	{
		HomeLocation = GetActorLocation();
	}
	
	if (UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponent())
	{
		// 掉血时触发受击事件；死亡判定在 AttributeSet 里（NewValue==0 时不发）。
		ASC->GetGameplayAttributeValueChangeDelegate(UFirstAttributeSet::GetHealthAttribute()).AddUObject(this, &ThisClass::HandleHealthChanged);
	}
	// 出生背剑：生成剑并挂到背部挂点，注册进战斗组件（先不当作装备）。
	if (BossWeaponClass && GetMesh() && BossCombatComponent)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.Instigator = this;

		if (ABossWeapon* Weapon = GetWorld()->SpawnActor<ABossWeapon>(
			BossWeaponClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams))
		{
			Weapon->AttachToComponent(
				GetMesh(),
				FAttachmentTransformRules::SnapToTargetNotIncludingScale,
				BackSocketName);

			BossCombatComponent->RegisterSpawnedWeapon(
				MyGameplayTags::Boss_Weapon_Sword,
				Weapon,
				false);
		}
	}
}

void ABossCharacter::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
	// 只有血量下降且还没死透，才触发受击（死亡交给 GA_Boss_Death）。
	if (Data.NewValue < Data.OldValue && Data.NewValue > 0.f)
	{
		FGameplayEventData EventData;
		EventData.Instigator = GetInstigator();

		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
			this,
			MyGameplayTags::Boss_Event_HitReact,
			EventData);
	}
}

void ABossCharacter::HandlePoiseRecoveryExpired()
{
	UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponent();
	if (!ASC || ASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead))
	{
		return;
	}

	// 正在配对处决时计时器本应已清除；这里再做一次安全保护。
	if (ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_BeingExecuted))
	{
		return;
	}

	if (ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Executable))
	{
		FGameplayEventData ExpiredEvent;
		ExpiredEvent.Instigator = this;
		ExpiredEvent.Target = this;

		// GameplayEvent 同步通知 Executable Ability 退出，再恢复数值。
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(this,MyGameplayTags::Boss_Event_ExecutableExpired,ExpiredEvent);
	}

	RestorePoiseToFull();
}

void ABossCharacter::SetMovementSpeed(float NewSpeed)
{
	if (GetCharacterMovement())
	{
		GetCharacterMovement()->MaxWalkSpeed = NewSpeed;
	}
}
