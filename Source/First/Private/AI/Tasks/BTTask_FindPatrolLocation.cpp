// Fill out your copyright notice in the Description page of Project Settings.


#include "AI/Tasks/BTTask_FindPatrolLocation.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/BossCharacter.h"
#include "NavigationSystem.h"

UBTTask_FindPatrolLocation::UBTTask_FindPatrolLocation()
{
	NodeName = TEXT("Find Patrol Location");
}

EBTNodeResult::Type UBTTask_FindPatrolLocation::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	ABossCharacter* Boss = AIController
		? Cast<ABossCharacter>(AIController->GetPawn())
		: nullptr;

	if (!AIController || !BB || !Boss)
	{
		return EBTNodeResult::Failed;
	}

	const FVector HomeLocation = BB->GetValueAsVector(HomeLocationKey);
	FVector OutLocation = HomeLocation;

	// 【UE 5.6 修正】随机可达点查询在 UNavigationSystemV1 上，不在 UAIBlueprintHelperLibrary。
	// 在 NavMesh 上取家点半径内的随机可达点；
	// 失败（例如没有 NavMesh）时保持回退点 = 家点，任务仍然成功。
	UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(Boss->GetWorld());
	FNavLocation NavLocation;
	if (NavSystem && NavSystem->GetRandomReachablePointInRadius(
		HomeLocation,
		Boss->PatrolRadius,
		NavLocation))
	{
		OutLocation = NavLocation.Location;
	}

	BB->SetValueAsVector(PatrolLocationKey, OutLocation);
	return EBTNodeResult::Succeeded;
}
