// Fill out your copyright notice in the Description page of Project Settings.


#include "Controller/BossAIController.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameplayCueNotifyTypes.h"
#include "MyGameplayTags.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/BossCharacter.h"
#include "Character/DKCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"

ABossAIController::ABossAIController()
{
	// AIController 默认会创建黑键组件和脑组件；
	// 这里替换成行为树专用的两个组件，是标准的 BehaviorTree 控制器写法。
	Blackboard = CreateDefaultSubobject<UBlackboardComponent>(TEXT("BossBlackboard"));
	BrainComponent = CreateDefaultSubobject<UBehaviorTreeComponent>(TEXT("BossBrain"));

	// —— 视觉感知配置 ——
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("BossSightConfig"));
	SightConfig->SightRadius = SightRadius;
	SightConfig->LoseSightRadius = LoseSightRadius;
	SightConfig->PeripheralVisionAngleDegrees = PeripheralVisionAngleDegrees;

	// MaxAge 控制视觉刺激记忆何时过期，不是“看不见后的失败回调延迟”。
	// 正式入战后是否脱战由目标死亡、失效或离开 NavMesh 决定。
	SightConfig->SetMaxAge(LoseSightTime);

	// 本阶段不区分队伍：玩家和 BOSS 都按“敌方/中立”处理，保证能互相感知。
	// 以后有多阵营（队友、多 BOSS）时再开 GenericTeamId。
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = false;

	// AIController 默认自带 PerceptionComponent，这里只把视觉配置挂上去。
	PerceptionComponent = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("BossPerception"));
	PerceptionComponent->ConfigureSense(*SightConfig);
	PerceptionComponent->SetDominantSense(UAISense_Sight::StaticClass());
}

ABossCharacter* ABossAIController::GetBossCharacter() const
{
	return Cast<ABossCharacter>(GetPawn());
}

bool ABossAIController::ShouldRetainCombatTargetAfterSightLoss(
	const UBlackboardComponent& BlackboardComponent) const
{
	ABossCharacter* Boss = GetBossCharacter();
	UAbilitySystemComponent* BossASC =
		Boss ? Boss->GetAbilitySystemComponent() : nullptr;

	if (!BossASC ||
		BossASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead))
	{
		return false;
	}

	// 处决事务期间（破韧定格 / 被处决 / 起立）视觉丢失不作为退出战斗的依据：
	// 处决是贴脸交互，BOSS 刚被玩家当面处决，"不知道玩家在哪"不成立；
	// 战斗状态由收尾的 ForceEngageTarget 重建。延迟丢失消费路径同样受益。
	if (BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Executable) ||
		BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_BeingExecuted) ||
		BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Executed))
	{
		return true;
	}

	// bShouldChase 覆盖“警戒结束、拔剑事件刚刚发出”的同帧窗口；
	// DrawSword/WeaponDrawn 则是不会被普通行为树切换清掉的入战事实。
	return BlackboardComponent.GetValueAsBool(TEXT("bShouldChase")) ||
		BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_DrawSword) ||
		BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_WeaponDrawn);
}

void ABossAIController::OnPossess(APawn* InPawn)
{
	// Controller 可能被重新用于另一只 Pawn；先解除旧 ASC 的监听。
	UnbindDamageReceivedEvent();
	NavigationBlockedTarget.Reset();
	CachedReachabilityTarget.Reset();
	CachedReachabilityQueryTime = -1.0;

	Super::OnPossess(InPawn);

	if (!GetBossCharacter())
	{
		return;
	}

	if (PerceptionComponent)
	{
		// 先移除再绑定，避免重新占有时出现重复回调。
		PerceptionComponent->OnTargetPerceptionUpdated.RemoveDynamic(
			this,
			&ThisClass::HandleTargetPerceptionUpdated);
		PerceptionComponent->OnTargetPerceptionUpdated.AddDynamic(
			this,
			&ThisClass::HandleTargetPerceptionUpdated);
	}

	InitializeBossBlackboard();
	BindDamageReceivedEvent();
}

void ABossAIController::OnUnPossess()
{
	UnbindDamageReceivedEvent();
	bHasDeferredPerceptionLoss = false;
	DeferredLostTarget.Reset();
	NavigationBlockedTarget.Reset();
	CachedReachabilityTarget.Reset();
	CachedReachabilityQueryTime = -1.0;

	if (PerceptionComponent)
	{
		PerceptionComponent->OnTargetPerceptionUpdated.RemoveDynamic(
			this,
			&ThisClass::HandleTargetPerceptionUpdated);
	}

	Super::OnUnPossess();
}

void ABossAIController::SetTargetNavigationBlocked(
	AActor* Target,
	bool bBlocked)
{
	AActor* PreviousBlockedTarget = NavigationBlockedTarget.Get();

	if (bBlocked)
	{
		if (!IsValid(Target))
		{
			return;
		}

		NavigationBlockedTarget = Target;
		if (PreviousBlockedTarget != Target)
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[BossAITrace] Navigation blocked | Target=%s"),
				*GetNameSafe(Target));
		}
		return;
	}

	if (!Target || NavigationBlockedTarget.Get() == Target)
	{
		if (IsValid(PreviousBlockedTarget))
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[BossAITrace] Navigation restored | Target=%s"),
				*GetNameSafe(PreviousBlockedTarget));
		}
		NavigationBlockedTarget.Reset();
	}
}

bool ABossAIController::IsTargetNavigationBlocked(const AActor* Target) const
{
	return IsValid(Target) && NavigationBlockedTarget.Get() == Target;
}

bool ABossAIController::RefreshTargetNavigationBlocked(AActor* Target)
{
	if (!IsValid(Target))
	{
		return false;
	}

	ABossCharacter* Boss = GetBossCharacter();
	UWorld* World = GetWorld();
	if (!Boss || !World)
	{
		SetTargetNavigationBlocked(Target, true);
		return true;
	}

	const FVector BossLocation = Boss->GetNavAgentLocation();
	const FVector TargetLocation = Target->GetActorLocation();
	const double CurrentTime = World->GetTimeSeconds();
	const double CacheInterval = FMath::Max(
		static_cast<double>(PathReachabilityRefreshInterval),
		0.0);
	const float MovementTolerance = FMath::Max(
		PathReachabilityMovementTolerance,
		0.f);
	const float MovementToleranceSquared = FMath::Square(MovementTolerance);
	const bool bCanReuseCachedResult =
		CachedReachabilityTarget.Get() == Target &&
		CachedReachabilityQueryTime >= 0.0 &&
		CurrentTime >= CachedReachabilityQueryTime &&
		CurrentTime - CachedReachabilityQueryTime <= CacheInterval &&
		FVector::DistSquared(BossLocation, CachedReachabilityBossLocation) <=
			MovementToleranceSquared &&
		FVector::DistSquared(TargetLocation, CachedReachabilityTargetLocation) <=
			MovementToleranceSquared;

	if (bCanReuseCachedResult)
	{
		SetTargetNavigationBlocked(Target, bCachedTargetNavigationBlocked);
		return bCachedTargetNavigationBlocked;
	}

	const bool bWasBlocked = IsTargetNavigationBlocked(Target);
	const float ExitTolerance = FMath::Max(NavMeshExitTolerance, 0.f);
	const float ReentryTolerance = FMath::Clamp(
		NavMeshReentryTolerance,
		0.f,
		ExitTolerance);
	const float HorizontalTolerance = bWasBlocked
		? ReentryTolerance
		: ExitTolerance;
	const float HorizontalQueryExtent = FMath::Max(HorizontalTolerance, 1.f);
	const float VerticalQueryExtent = FMath::Max(
		NavMeshVerticalProjectionExtent,
		1.f);

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
	FNavLocation ProjectedLocation;
	const bool bProjectionSucceeded = NavSys && NavSys->ProjectPointToNavigation(
		TargetLocation,
		ProjectedLocation,
		FVector(
			HorizontalQueryExtent,
			HorizontalQueryExtent,
			VerticalQueryExtent),
		&GetNavAgentPropertiesRef());

	const float HorizontalProjectionDistance = bProjectionSucceeded
		? FVector::Dist2D(TargetLocation, ProjectedLocation.Location)
		: TNumericLimits<float>::Max();
	const bool bTargetOnNavigation = bProjectionSucceeded &&
		HorizontalProjectionDistance <=
			HorizontalTolerance + KINDA_SMALL_NUMBER;

	// 目标点属于 NavMesh 仍不代表与 BOSS 位于同一个连通区域。
	// 要求同步查询得到完整、非 Partial 的路径，避免隔门、断岛或跨层永久追逐。
	UNavigationPath* Path = bTargetOnNavigation
		? UNavigationSystemV1::FindPathToLocationSynchronously(
			World,
			BossLocation,
			ProjectedLocation.Location,
			Boss)
		: nullptr;
	const bool bAlreadyAtTarget = bTargetOnNavigation &&
		FVector::DistSquared2D(BossLocation, ProjectedLocation.Location) <=
		FMath::Square(FMath::Max(PathReachabilityGoalTolerance, 0.f));
	const bool bHasCompletePath = bAlreadyAtTarget ||
		(Path && Path->IsValid() && !Path->IsPartial());
	const bool bBlocked = !bTargetOnNavigation || !bHasCompletePath;

	CachedReachabilityTarget = Target;
	CachedReachabilityBossLocation = BossLocation;
	CachedReachabilityTargetLocation = TargetLocation;
	CachedReachabilityQueryTime = CurrentTime;
	bCachedTargetNavigationBlocked = bBlocked;

	SetTargetNavigationBlocked(Target, bBlocked);
	return bBlocked;
}

void ABossAIController::BindDamageReceivedEvent()
{
	UnbindDamageReceivedEvent();

	ABossCharacter* Boss = GetBossCharacter();
	UAbilitySystemComponent* BossASC =
		Boss ? Boss->GetAbilitySystemComponent() : nullptr;

	if (!BossASC)
	{
		return;
	}

	DamageReceivedEventASC = BossASC;
	DamageReceivedEventHandle =
		BossASC->GenericGameplayEventCallbacks
		.FindOrAdd(MyGameplayTags::Shared_Event_DamageReceived)
		.AddUObject(this, &ThisClass::HandleDamageReceived);
}

void ABossAIController::UnbindDamageReceivedEvent()
{
	if (DamageReceivedEventHandle.IsValid())
	{
		if (UAbilitySystemComponent* BossASC = DamageReceivedEventASC.Get())
		{
			if (FGameplayEventMulticastDelegate* EventDelegate =
				BossASC->GenericGameplayEventCallbacks.Find(
					MyGameplayTags::Shared_Event_DamageReceived))
			{
				EventDelegate->Remove(DamageReceivedEventHandle);
			}
		}
	}

	DamageReceivedEventHandle.Reset();
	DamageReceivedEventASC.Reset();
}

ADKCharacter* ABossAIController::ResolveDamageInstigator(
	const FGameplayEventData& Payload) const
{
	const auto ResolveCandidate =
		[](AActor* SourceActor) -> ADKCharacter*
		{
			if (!IsValid(SourceActor))
			{
				return nullptr;
			}

			if (ADKCharacter* Player = Cast<ADKCharacter>(SourceActor))
			{
				return Player;
			}

			if (AController* SourceController = Cast<AController>(SourceActor))
			{
				if (ADKCharacter* Player =
					Cast<ADKCharacter>(SourceController->GetPawn()))
				{
					return Player;
				}
			}

			if (APawn* InstigatorPawn = SourceActor->GetInstigator())
			{
				if (ADKCharacter* Player = Cast<ADKCharacter>(InstigatorPawn))
				{
					return Player;
				}
			}

			if (AActor* OwnerActor = SourceActor->GetOwner())
			{
				if (ADKCharacter* Player = Cast<ADKCharacter>(OwnerActor))
				{
					return Player;
				}

				if (AController* OwnerController = Cast<AController>(OwnerActor))
				{
					return Cast<ADKCharacter>(OwnerController->GetPawn());
				}
			}

			return nullptr;
		};

	if (ADKCharacter* Player = ResolveCandidate(
		const_cast<AActor*>(Payload.Instigator.Get())))
	{
		return Player;
	}

	if (ADKCharacter* Player = ResolveCandidate(
		Payload.ContextHandle.GetOriginalInstigator()))
	{
		return Player;
	}

	return ResolveCandidate(Payload.ContextHandle.GetEffectCauser());
}

void ABossAIController::ForceEngageTarget(AActor* Target)
{
	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!BB || !IsValid(Target))
	{
		return;
	}

	ABossCharacter* Boss = GetBossCharacter();
	UAbilitySystemComponent* BossASC =
		Boss ? Boss->GetAbilitySystemComponent() : nullptr;

	// 死亡收尾由感知/死亡分支清理，不由这里拉回战斗。
	if (!Boss || !BossASC ||
		BossASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead))
	{
		return;
	}

	// 与 HandleDamageReceived 同口径：真实确认了目标时，
	// 旧目标的不可达抑制不能污染新目标。
	if (NavigationBlockedTarget.IsValid() &&
		NavigationBlockedTarget.Get() != Target)
	{
		NavigationBlockedTarget.Reset();
	}

	BB->SetValueAsObject(TEXT("TargetActor"), Target);

	// 目标不可达时只保留目标、不开追击；距离服务会持续复查，
	// 回到 NavMesh 后由感知/服务恢复完整战斗状态。
	if (RefreshTargetNavigationBlocked(Target))
	{
		BB->SetValueAsBool(TEXT("bPlayerDetected"), false);
		BB->SetValueAsBool(TEXT("bShouldChase"), false);
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[BossAITrace] Force engage suppressed by navigation | Target=%s"),
			*GetNameSafe(Target));
		return;
	}

	bHasDeferredPerceptionLoss = false;
	DeferredLostTarget.Reset();

	// 不在这里 SetFocus：处决期间 UpdateBossState 会清掉它，
	// 起立结束后服务的 FaceTarget 分支会自动面向玩家并接管旋转。
	BB->SetValueAsBool(TEXT("bPlayerDetected"), true);
	BB->SetValueAsBool(TEXT("bShouldChase"), true);
	UE_LOG(
		LogTemp,
		Display,
		TEXT("[BossAITrace] Force engage after execution | Target=%s"),
		*GetNameSafe(Target));
}

void ABossAIController::HandleDamageReceived(
	const FGameplayEventData* Payload)
{
	if (!HasAuthority() ||
		!Payload ||
		!FMath::IsFinite(Payload->EventMagnitude) ||
		Payload->EventMagnitude <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	ABossCharacter* Boss = GetBossCharacter();
	UAbilitySystemComponent* BossASC =
		Boss ? Boss->GetAbilitySystemComponent() : nullptr;
	UBlackboardComponent* BB = GetBlackboardComponent();

	if (!Boss || !BossASC || !BB)
	{
		return;
	}

	if (const AActor* EventTarget = Payload->Target.Get())
	{
		if (EventTarget != Boss)
		{
			return;
		}
	}

	// 处决和死亡事务拥有更高优先级，普通伤害不能把它们拉回战斗。
	if (BossASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead) ||
		BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Executable) ||
		BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_BeingExecuted) ||
		BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Executed))
	{
		return;
	}

	ADKCharacter* Attacker = ResolveDamageInstigator(*Payload);
	if (!IsValid(Attacker))
	{
		return;
	}

	// 真实伤害确认了新的攻击者时，旧目标的不可达抑制不能污染新目标。
	if (NavigationBlockedTarget.IsValid() &&
		NavigationBlockedTarget.Get() != Attacker)
	{
		NavigationBlockedTarget.Reset();
	}

	if (UAbilitySystemComponent* AttackerASC =
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Attacker))
	{
		if (AttackerASC->HasMatchingGameplayTag(
			MyGameplayTags::Shared_Status_Dead))
		{
			return;
		}
	}

	// 伤害回调发生在行为树服务 Tick 之间。这里必须同步复查导航状态；
	// 否则玩家刚被攻击吸附带出 NavMesh 时，会先触发强制转身，再被服务打回巡逻。
	const bool bAttackerNavigationBlocked =
		RefreshTargetNavigationBlocked(Attacker);

	// 新的真实伤害重新确认了攻击者，旧的延迟视觉丢失不再有效。
	bHasDeferredPerceptionLoss = false;
	DeferredLostTarget.Reset();

	BB->SetValueAsObject(TEXT("TargetActor"), Attacker);

	// 不可达目标仍保留给距离服务复查；在它回到 NavMesh 前不能重新抢占巡逻朝向。
	if (bAttackerNavigationBlocked)
	{
		BB->SetValueAsBool(TEXT("bPlayerDetected"), false);
		BB->SetValueAsBool(TEXT("bShouldChase"), false);
		BB->SetValueAsBool(TEXT("bProvokedByDamage"), false);
		ClearFocus(EAIFocusPriority::Gameplay);
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[BossAITrace] Damage alert suppressed by navigation | "
				 "Attacker=%s | Damage=%.2f"),
			*GetNameSafe(Attacker),
			Payload->EventMagnitude);
		return;
	}

	BB->SetValueAsBool(TEXT("bPlayerDetected"), true);
	SetFocus(Attacker, EAIFocusPriority::Gameplay);

	// 已经追击或正在处理同一请求时只刷新目标，不能重置特殊转身计时。
	const bool bAlreadyEngaged =
		BB->GetValueAsBool(TEXT("bShouldChase")) ||
		BB->GetValueAsBool(TEXT("bProvokedByDamage"));

	if (bAlreadyEngaged)
	{
		return;
	}

	StopMovement();
	if (UCharacterMovementComponent* Movement = Boss->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}

	Boss->SetBossRotationMode(EBossRotationMode::Frozen);
	BB->SetValueAsBool(TEXT("bHasAttacked"), false);
	BB->SetValueAsBool(TEXT("bStrafeTimeout"), false);
	BB->SetValueAsBool(TEXT("bShouldChase"), false);

	// 最后写入观察键，让行为树立即中止低优先级分支。
	UE_LOG(
		LogTemp,
		Display,
		TEXT("[BossAITrace] Damage alert requested | Attacker=%s | Damage=%.2f"),
		*GetNameSafe(Attacker),
		Payload->EventMagnitude);
	BB->SetValueAsBool(TEXT("bProvokedByDamage"), true);
}

void ABossAIController::HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!BB || !Actor)
	{
		return;
	}

	// 只把“带 GAS 的角色”当目标，排除自己、武器、特效等无关 Actor。
	ABaseCharacter* Candidate = Cast<ABaseCharacter>(Actor);
	if (!Candidate || Candidate == GetPawn())
	{
		return;
	}

	// 死亡目标不能被重新写入黑板。
	UAbilitySystemComponent* CandidateASC =
		UAbilitySystemBlueprintLibrary::
		GetAbilitySystemComponent(Candidate);

	const bool bCandidateDead = CandidateASC &&
		CandidateASC->HasMatchingGameplayTag(
			MyGameplayTags::Shared_Status_Dead);

	if (bCandidateDead)
	{
		SetTargetNavigationBlocked(Candidate, false);

		// 若死亡者恰好还是当前目标，立即清干净；
		// 若不是当前目标，则只拒绝本次写入。
		AActor* CurrentTarget = Cast<AActor>(
			BB->GetValueAsObject(TEXT("TargetActor")));

		if (CurrentTarget == Candidate)
		{
			StopMovement();
			ClearFocus(EAIFocusPriority::Gameplay);
			SetTargetNavigationBlocked(Candidate, false);

			BB->SetValueAsObject(TEXT("TargetActor"), nullptr);
			BB->SetValueAsBool(TEXT("bPlayerDetected"), false);
			BB->SetValueAsBool(TEXT("bInAlertRange"), false);
			BB->SetValueAsBool(TEXT("bInAttackRange"), false);
			BB->SetValueAsBool(TEXT("bPlayerTooClose"), false);
			BB->SetValueAsBool(TEXT("bInStrafeRange"), false);
			BB->SetValueAsBool(TEXT("bPlayerTooFar"), false);
			BB->SetValueAsBool(TEXT("bShouldChase"), false);
			BB->SetValueAsBool(TEXT("bStrafeTimeout"), false);
			BB->SetValueAsBool(TEXT("bHasAttacked"), false);
			BB->SetValueAsBool(TEXT("bProvokedByDamage"), false);
			bHasDeferredPerceptionLoss = false;
			DeferredLostTarget.Reset();
		}

		return;
	}

	if (Stimulus.WasSuccessfullySensed())
	{
		if (bHasDeferredPerceptionLoss &&
			DeferredLostTarget.Get() == Candidate)
		{
			bHasDeferredPerceptionLoss = false;
			DeferredLostTarget.Reset();
		}

		// 强制入战期间，真实伤害来源不能被其它视觉候选覆盖。
		AActor* DamageTarget = Cast<AActor>(
			BB->GetValueAsObject(TEXT("TargetActor")));
		if (BB->GetValueAsBool(TEXT("bProvokedByDamage")) &&
			IsValid(DamageTarget) &&
			DamageTarget != Candidate)
		{
			return;
		}

		// 感知回调不能先 SetFocus、再等待距离服务判定导航状态。
		// 同步复查可关闭“感知抢朝向 → 服务清朝向”的周期性扭头竞态。
		if (RefreshTargetNavigationBlocked(Candidate))
		{
			BB->SetValueAsObject(TEXT("TargetActor"), Candidate);
			BB->SetValueAsBool(TEXT("bPlayerDetected"), false);
			BB->SetValueAsBool(TEXT("bShouldChase"), false);
			BB->SetValueAsBool(TEXT("bProvokedByDamage"), false);
			ClearFocus(EAIFocusPriority::Gameplay);
			return;
		}

		// 接受另一个视觉目标时，旧目标的不可达抑制不再属于当前索敌事务。
		if (NavigationBlockedTarget.IsValid() &&
			NavigationBlockedTarget.Get() != Candidate)
		{
			NavigationBlockedTarget.Reset();
		}

		// 感知层只写入“看见了谁”。是否拥有 Focus 由警戒/追击/受袭任务决定；
		// 否则玩家仍在警戒范围外时，巡逻朝向会与残留 Focus 争夺控制权。
		BB->SetValueAsObject(TEXT("TargetActor"), Candidate);
		BB->SetValueAsBool(TEXT("bPlayerDetected"), true);
	}
	else
	{
		// 目标从感知中消失（MaxAge 到期）：只有清掉的是当前目标时才重置状态。
		AActor* CurrentTarget = Cast<AActor>(BB->GetValueAsObject(TEXT("TargetActor")));
		if (CurrentTarget == Candidate)
		{
			// 处决事务（破韧定格 / 被处决 / 起立）期间视觉丢失一律不处理：
			// 下面任何清理分支（含导航抑制清理）都会把战斗目标清空，
			// 导致起身后退回巡逻；起立收尾由 GA 主动 ForceEngage 重建仇恨。
			if (ABossCharacter* Boss = GetBossCharacter())
			{
				UAbilitySystemComponent* BossASC = Boss->GetAbilitySystemComponent();
				if (BossASC &&
					(BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Executable) ||
					 BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_BeingExecuted) ||
					 BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Executed)))
				{
					return;
				}
			}

			// 导航不可达是独立于视觉感知的抑制状态。视觉丢失不能擅自解除它，
			// 否则下一次感知成功会再次 SetFocus，造成巡逻与注视来回抢旋转。
			if (RefreshTargetNavigationBlocked(Candidate))
			{
				BB->SetValueAsBool(TEXT("bPlayerDetected"), false);
				BB->SetValueAsBool(TEXT("bInAlertRange"), false);
				BB->SetValueAsBool(TEXT("bInAttackRange"), false);
				BB->SetValueAsBool(TEXT("bPlayerTooClose"), false);
				BB->SetValueAsBool(TEXT("bInStrafeRange"), false);
				BB->SetValueAsBool(TEXT("bPlayerTooFar"), false);
				BB->SetValueAsBool(TEXT("bShouldChase"), false);
				BB->SetValueAsBool(TEXT("bStrafeTimeout"), false);
				BB->SetValueAsBool(TEXT("bHasAttacked"), false);
				BB->SetValueAsBool(TEXT("bProvokedByDamage"), false);
				bHasDeferredPerceptionLoss = false;
				DeferredLostTarget.Reset();
				ClearFocus(EAIFocusPriority::Gameplay);

				if (ABossCharacter* Boss = GetBossCharacter())
				{
					Boss->SetBossRotationMode(
						EBossRotationMode::OrientToMovement);
				}
				return;
			}

			// 强制转身和拔剑尚未结束时先保留目标，但记录这次丢失；
			// 任务结束后会消费记录，避免永久保留过期目标。
			if (BB->GetValueAsBool(TEXT("bProvokedByDamage")))
			{
				bHasDeferredPerceptionLoss = true;
				DeferredLostTarget = Candidate;
				return;
			}

			// 视觉只负责首次发现玩家。警戒已经提交追击、正在拔剑或已经拔剑时，
			// 玩家绕到身后不等于离开 BOSS 领地，必须继续保留战斗目标。
			// 不在这里强制 SetFocus；攻击方向锁定和旋转模式仍由 UpdateBossState 统一管理。
			if (ShouldRetainCombatTargetAfterSightLoss(*BB))
			{
				BB->SetValueAsBool(TEXT("bPlayerDetected"), true);
				BB->SetValueAsBool(TEXT("bShouldChase"), true);
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[BossAITrace] Sight loss ignored during locked combat | Target=%s"),
					*GetNameSafe(Candidate));
				return;
			}

			BB->SetValueAsObject(TEXT("TargetActor"), nullptr);
			BB->SetValueAsBool(TEXT("bPlayerDetected"), false);
			BB->SetValueAsBool(TEXT("bInAlertRange"), false);
			BB->SetValueAsBool(TEXT("bInAttackRange"), false);
			BB->SetValueAsBool(TEXT("bPlayerTooClose"), false);
			BB->SetValueAsBool(TEXT("bInStrafeRange"), false);
			BB->SetValueAsBool(TEXT("bPlayerTooFar"), false);
			BB->SetValueAsBool(TEXT("bShouldChase"), false);
			BB->SetValueAsBool(TEXT("bStrafeTimeout"), false);
			BB->SetValueAsBool(TEXT("bHasAttacked"), false);
			BB->SetValueAsBool(TEXT("bProvokedByDamage"), false);
			bHasDeferredPerceptionLoss = false;
			DeferredLostTarget.Reset();
			SetTargetNavigationBlocked(Candidate, false);
			ClearFocus(EAIFocusPriority::Gameplay);

			if (ABossCharacter* Boss = GetBossCharacter())
			{
				// 先冻结丢失目标瞬间可能残留的路径转向；
				// UpdateBossState 下一 Tick 会在空闲状态恢复 OrientToMovement。
				Boss->SetBossRotationMode(EBossRotationMode::Frozen);
			}
		}
	}
}

bool ABossAIController::ResolveDeferredPerceptionLoss()
{
	if (!bHasDeferredPerceptionLoss)
	{
		return false;
	}

	UBlackboardComponent* BB = GetBlackboardComponent();
	AActor* LostTarget = DeferredLostTarget.Get();

	// 先消费记录，使重复清理保持幂等。
	bHasDeferredPerceptionLoss = false;
	DeferredLostTarget.Reset();

	if (!BB)
	{
		return false;
	}

	AActor* CurrentTarget = Cast<AActor>(
		BB->GetValueAsObject(TEXT("TargetActor")));

	// 延迟期间已经切换到另一个有效目标时，不能用旧记录清掉新目标。
	if (IsValid(CurrentTarget) &&
		((IsValid(LostTarget) && CurrentTarget != LostTarget) ||
		 !IsValid(LostTarget)))
	{
		return false;
	}

	// 受袭转身/拔剑期间可能先收到视觉丢失。若任务结束时战斗已经锁定，
	// 只消费延迟记录，不允许这条旧感知事件把刚建立的战斗目标清掉。
	if (IsValid(CurrentTarget) &&
		!RefreshTargetNavigationBlocked(CurrentTarget) &&
		ShouldRetainCombatTargetAfterSightLoss(*BB))
	{
		BB->SetValueAsBool(TEXT("bPlayerDetected"), true);
		BB->SetValueAsBool(TEXT("bShouldChase"), true);
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[BossAITrace] Deferred sight loss ignored during locked combat | Target=%s"),
			*GetNameSafe(CurrentTarget));
		return false;
	}

	StopMovement();
	ClearFocus(EAIFocusPriority::Gameplay);

	BB->SetValueAsObject(TEXT("TargetActor"), nullptr);
	BB->SetValueAsBool(TEXT("bPlayerDetected"), false);
	BB->SetValueAsBool(TEXT("bInAlertRange"), false);
	BB->SetValueAsBool(TEXT("bInAttackRange"), false);
	BB->SetValueAsBool(TEXT("bPlayerTooClose"), false);
	BB->SetValueAsBool(TEXT("bInStrafeRange"), false);
	BB->SetValueAsBool(TEXT("bPlayerTooFar"), false);
	BB->SetValueAsBool(TEXT("bShouldChase"), false);
	BB->SetValueAsBool(TEXT("bStrafeTimeout"), false);
	BB->SetValueAsBool(TEXT("bHasAttacked"), false);
	BB->SetValueAsBool(TEXT("bProvokedByDamage"), false);
	SetTargetNavigationBlocked(nullptr, false);

	if (ABossCharacter* Boss = GetBossCharacter())
	{
		Boss->SetBossRotationMode(EBossRotationMode::Frozen);
	}

	return true;
}

void ABossAIController::InitializeBossBlackboard()
{
	ABossCharacter* Boss = GetBossCharacter();
	UBlackboardComponent* BB = GetBlackboardComponent();

	if (!Boss)
	{
		UE_LOG(LogTemp, Error, TEXT("[BossAI] 缺少 BOSS 角色（请确认 AI 控制器类挂在 BP_Boss 上）"));
		return;
	}
	if (!BB)
	{
		UE_LOG(LogTemp, Error, TEXT("[BossAI] 缺少黑键组件"));
		return;
	}
	if (!BossBehaviorTree)
	{
		UE_LOG(LogTemp, Error, TEXT("[BossAI] 缺少行为树资产（请在 BP_BossAIController 类默认值中把 BossBehaviorTree 设为 BT_Boss）"));
		return;
	}

	// 先让黑键组件使用行为树配套的黑板资产，之后才能写入键值。
	// 顺序不能反：先 UseBlackboard，再 SetValueAsVector。
	if (UBlackboardData* BBAsset = BossBehaviorTree->GetBlackboardAsset())
	{
		UseBlackboard(BBAsset,BB);
	}

	// 巡逻初值：家点用出生位置，目标点先给当前位置，避免第一帧找点前为空。
	BB->SetValueAsVector(TEXT("HomeLocation"), Boss->HomeLocation);
	BB->SetValueAsVector(TEXT("PatrolLocation"), Boss->GetActorLocation());

	// 索敌初值。
	BB->SetValueAsObject(TEXT("TargetActor"), nullptr);
	BB->SetValueAsBool(TEXT("bPlayerDetected"), false);
	BB->SetValueAsBool(TEXT("bInAlertRange"), false);
	BB->SetValueAsBool(TEXT("bInAttackRange"), false);
	BB->SetValueAsBool(TEXT("bShouldChase"), false);
	BB->SetValueAsBool(TEXT("bIsDead"), false);
	BB->SetValueAsBool(TEXT("bProvokedByDamage"), false);
	bHasDeferredPerceptionLoss = false;
	DeferredLostTarget.Reset();
	NavigationBlockedTarget.Reset();
	CachedReachabilityTarget.Reset();
	CachedReachabilityQueryTime = -1.0;


	RunBehaviorTree(BossBehaviorTree);
}
