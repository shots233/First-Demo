// Fill out your copyright notice in the Description page of Project Settings.


#include "AI/Tasks/BTTask_BossGetBackOffLocation.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/BossCharacter.h"
#include "NavigationSystem.h"

namespace
{
	// 后退路径被挡住时，从命中点再往回留的安全余量（不贴着 NavMesh 边缘停）。
	constexpr float BackOffHitMargin = 60.f;
	// 可用后退距离低于该值视为"没有后退空间"（背靠墙/角落），
	// 此时改算侧向绕位点，避免本分支原地空转导致 BOSS 站桩。
	constexpr float MinUsefulBackOffDistance = 120.f;
	// 后退点 NavMesh 投影容差（水平 / 垂直）。
	constexpr float BackOffProjectionExtent = 100.f;
	constexpr float BackOffVerticalProjectionExtent = 200.f;
	// 无后退空间时侧向绕位的半径档位（相对周旋距离的比例）。
	constexpr float BackOffSideRadiusScales[] = { 0.6f, 0.4f };
}

UBTTask_BossGetBackOffLocation::UBTTask_BossGetBackOffLocation()
{
	NodeName = TEXT("Boss Get Back Off Location");
}

EBTNodeResult::Type UBTTask_BossGetBackOffLocation::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// 和侧移任务一样：先把黑板、控制器、BOSS、目标取出来。
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	ABossCharacter* Boss = AIController
		? Cast<ABossCharacter>(AIController->GetPawn())
		: nullptr;
	AActor* Target = BB
		? Cast<AActor>(BB->GetValueAsObject(TEXT("TargetActor")))
		: nullptr;

	// 缺东西就直接失败，让行为树回退，不要硬算。
	if (!BB || !Boss || !Target)
	{
		return EBTNodeResult::Failed;
	}

	// 1. "远离玩家"的方向 = 玩家位置 - BOSS 位置 反过来，即 BOSS 指向玩家的反方向。
	FVector AwayDirection = (Boss->GetActorLocation() - Target->GetActorLocation()).GetSafeNormal2D();
	// 2. 如果 BOSS 和玩家几乎重合（方向向量接近 0），就退回"BOSS 面朝的方向"作为兜底。
	if (AwayDirection.IsNearlyZero())
	{
		AwayDirection = Boss->GetActorForwardVector();
		AwayDirection.Z = 0.f;
		AwayDirection.Normalize();
	}

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(Boss->GetWorld());
	const FNavAgentProperties* AgentProperties = AIController
		? &AIController->GetNavAgentPropertiesRef()
		: nullptr;
	const FVector BossNavLocation = Boss->GetNavAgentLocation();

	auto ProjectToNavMesh = [&](const FVector& Candidate, FVector& OutLocation) -> bool
	{
		FNavLocation Projected;
		if (!NavSys ||
			!NavSys->ProjectPointToNavigation(
				Candidate,
				Projected,
				FVector(BackOffProjectionExtent, BackOffProjectionExtent, BackOffVerticalProjectionExtent),
				AgentProperties))
		{
			return false;
		}
		OutLocation = Projected.Location;
		return true;
	};

	// 理想落点：退到离玩家一个周旋距离的位置（与旧行为一致）。
	const FVector DesiredBackOffPoint =
		Target->GetActorLocation() + AwayDirection * Boss->GetStrafeRadius();

	// 3.【防站桩】后退路径必须是"沿 NavMesh 可直行"的一条线：
	//    NavigationRaycast 在离开 NavMesh / 被墙挡住的位置给出命中点（返回 false）。
	float ReachableDistance = Boss->GetStrafeRadius();
	FVector HitLocation;
	if (!NavSys)
	{
		// 没有 NavSystem：无法验证，保持全距（旧行为）。
	}
	else if (UNavigationSystemV1::NavigationRaycast(
		Boss->GetWorld(),
		BossNavLocation,
		DesiredBackOffPoint,
		HitLocation,
		nullptr,
		AIController))
	{
		// 无阻挡：全距可退。
	}
	else
	{
		// 被挡住：只退到命中点之前一点，保证退完仍站在 NavMesh 上。
		ReachableDistance = FMath::Max(
			FVector::Dist2D(BossNavLocation, HitLocation) - BackOffHitMargin,
			0.f);
	}

	// 4. 有足够的后退空间：使用截断后的后退点（投影纠偏到 NavMesh 上）。
	const FVector RetreatedPoint = BossNavLocation + AwayDirection * ReachableDistance;
	FVector BackOffLocation;
	if (ReachableDistance >= MinUsefulBackOffDistance &&
		ProjectToNavMesh(RetreatedPoint, BackOffLocation))
	{
		BB->SetValueAsVector(BackOffLocationKey, BackOffLocation);
		return EBTNodeResult::Succeeded;
	}

	// 5. 没有后退空间（背靠墙/角落）：改算侧向绕位点，保证本分支仍然能移动。
	//    侧向点同样要求"可直行 + 在 NavMesh 上"，两档半径、两个方向依次尝试。
	for (const float RadiusScale : BackOffSideRadiusScales)
	{
		const float SideRadius = Boss->GetStrafeRadius() * RadiusScale;
		for (const float SideSign : {-1.f, 1.f})
		{
			const FVector Perp(SideSign * -AwayDirection.Y, SideSign * AwayDirection.X, 0.f);
			const FVector SideCandidate = BossNavLocation + Perp * SideRadius;

			FVector SideHit;
			const bool bSideClear = !NavSys ||
				UNavigationSystemV1::NavigationRaycast(
					Boss->GetWorld(),
					BossNavLocation,
					SideCandidate,
					SideHit,
					nullptr,
					AIController);

			FVector SidePoint;
			if (bSideClear && ProjectToNavMesh(SideCandidate, SidePoint))
			{
				BB->SetValueAsVector(BackOffLocationKey, SidePoint);
				return EBTNodeResult::Succeeded;
			}
		}
	}

	// 6. 极端情况（几乎没有 NavMesh 数据）：退回旧行为的原始点，保证不比之前更差。
	BB->SetValueAsVector(BackOffLocationKey, DesiredBackOffPoint);
	return EBTNodeResult::Succeeded;
}
