// Fill out your copyright notice in the Description page of Project Settings.


#include "AI/Tasks/BTTask_BossGetStrafeLocation.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/BossCharacter.h"
#include "NavigationSystem.h"

namespace
{
	// 侧移点 NavMesh 投影容差：水平允许吸附 100 单位、垂直 200。
	constexpr float StrafeProjectionExtent = 100.f;
	constexpr float StrafeVerticalProjectionExtent = 200.f;
	// 两侧全距都无效时，缩小到该比例再试（贴墙窄道时仍能找到可走的点）。
	constexpr float StrafeFallbackRadiusScale = 0.6f;
}

UBTTask_BossGetStrafeLocation::UBTTask_BossGetStrafeLocation()
{
	NodeName = TEXT("Boss Get Strafe Location");
}

EBTNodeResult::Type UBTTask_BossGetStrafeLocation::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// 1. 先把要用到的东西从行为树/控制器/角色身上取出来。
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	ABossCharacter* Boss = AIController
		? Cast<ABossCharacter>(AIController->GetPawn()): nullptr;
	AActor* Target = BB
		? Cast<AActor>(BB->GetValueAsObject(TEXT("TargetActor")))
		: nullptr;

	// 2. 缺任何一样都说明当前状态不对（比如目标已丢失），任务直接失败，
	//    行为树会退回上一个分支（例如回巡逻），而不是继续往下算。
	if (!BB || !Boss || !Target)
	{
		return EBTNodeResult::Failed;
	}

	// 3. 计算"从 BOSS 指向玩家"的单位方向（只取水平 X/Y，去掉高度差）。
	const FVector ToTarget = (Target->GetActorLocation() - Boss->GetActorLocation()).GetSafeNormal2D();

	// 4. 随机选左侧或右侧（每次侧移方向都可能变，BOSS 看起来更自然）。
	const float Side = FMath::RandBool() ? 1.f : -1.f;

	// 5.【防站桩】侧移点必须落在 NavMesh 上，否则后面的 MoveTo 必定失败：
	//    战斗 Selector 里本分支失败后可能没有同距离带的其他分支可跑，
	//    整棵树就会空转，BOSS 原地发呆。因此按顺序尝试：
	//    首选边全距 → 换边全距 → 首选边 60% 距 → 换边 60% 距。
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(Boss->GetWorld());
	const FNavAgentProperties* AgentProperties = AIController
		? &AIController->GetNavAgentPropertiesRef()
		: nullptr;

	auto TryStrafePoint = [&](float SideSign, float Radius, FVector& OutLocation) -> bool
	{
		// 垂直向量公式：把方向向量 (X, Y) 旋转 90° 得到 (-Y, X)，乘 Side 切换左右。
		const FVector Perp(SideSign * -ToTarget.Y, SideSign * ToTarget.X, 0.f);
		const FVector Candidate = Target->GetActorLocation() + Perp * Radius;

		FNavLocation Projected;
		if (!NavSys ||
			!NavSys->ProjectPointToNavigation(
				Candidate,
				Projected,
				FVector(StrafeProjectionExtent, StrafeProjectionExtent, StrafeVerticalProjectionExtent),
				AgentProperties))
		{
			return false;
		}
		OutLocation = Projected.Location;
		return true;
	};

	FVector StrafeLocation;
	if (!TryStrafePoint(Side, Boss->GetStrafeRadius(), StrafeLocation) &&
		!TryStrafePoint(-Side, Boss->GetStrafeRadius(), StrafeLocation) &&
		!TryStrafePoint(Side, Boss->GetStrafeRadius() * StrafeFallbackRadiusScale, StrafeLocation) &&
		!TryStrafePoint(-Side, Boss->GetStrafeRadius() * StrafeFallbackRadiusScale, StrafeLocation))
	{
		// 全部投影失败（几乎没有 NavMesh 数据）：退回旧行为的原始点，
		// 保证不比之前更差。MoveTo 对部分路径仍会走出最近可到达处。
		const FVector Perp(Side * -ToTarget.Y, Side * ToTarget.X, 0.f);
		StrafeLocation = Target->GetActorLocation() + Perp * Boss->GetStrafeRadius();
	}

	// 6. 写进黑板，后面的"移至（Move To）"节点会读这个键去移动。
	BB->SetValueAsVector(StrafeLocationKey, StrafeLocation);
	return EBTNodeResult::Succeeded;
}
