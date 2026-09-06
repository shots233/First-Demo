// Fill out your copyright notice in the Description page of Project Settings.


#include "AI/Services/UBTService_UpdateMoveSpeed.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/BossCharacter.h"

UBTService_UpdateMoveSpeed::UBTService_UpdateMoveSpeed()
{
	NodeName = TEXT("Update Move Speed");
}

void UBTService_UpdateMoveSpeed::TickNode(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	ABossCharacter* Boss = AIController
		? Cast<ABossCharacter>(AIController->GetPawn())
		: nullptr;

	if (!BB || !Boss)
	{
		return;
	}

	// 靠近玩家时奔跑（500）；后退/侧移/巡逻用步行（250）。
	// bChasing：是否在追击状态（索敌手册写入的黑板键）。
	const bool bChasing = BB->GetValueAsBool(TEXT("bShouldChase"));
	// bTooFar：玩家是否在周旋距离（500）之外，由 UpdateCombatDistance 服务写入。
	const bool bTooFar = BB->GetValueAsBool(TEXT("bPlayerTooFar"));
	// 只有在"追击中 且 距离确实远"时才用奔跑速度，其余情况一律步行。
	const bool bUseRunSpeed = bChasing && bTooFar;

	// 把选好的速度写进角色移动组件（MaxWalkSpeed）。
	Boss->SetMovementSpeed(
		bUseRunSpeed ? Boss->ChaseMoveSpeed : Boss->PatrolMoveSpeed);
}
