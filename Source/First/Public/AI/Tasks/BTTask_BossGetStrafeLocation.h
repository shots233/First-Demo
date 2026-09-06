// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_BossGetStrafeLocation.generated.h"

/**
 * 计算玩家左右方向的侧移点（半径 = 周旋距离），写入黑板 StrafeLocation。
 */
UCLASS()
class FIRST_API UBTTask_BossGetStrafeLocation : public UBTTaskNode
{
	GENERATED_BODY()
public:
	UBTTask_BossGetStrafeLocation();

	UPROPERTY(EditAnywhere, Category="Boss|Strafe")
	FName StrafeLocationKey = TEXT("StrafeLocation");

	virtual EBTNodeResult::Type ExecuteTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;
	
	
};
