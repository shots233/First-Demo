// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_FindPatrolLocation.generated.h"

/**
 * 
 */
UCLASS()
class FIRST_API UBTTask_FindPatrolLocation : public UBTTaskNode
{
	GENERATED_BODY()
public:
	UBTTask_FindPatrolLocation();

	// 家点所在的黑板键。
	UPROPERTY(EditAnywhere, Category="Boss|Patrol")
	FName HomeLocationKey = TEXT("HomeLocation");

	// 本次巡逻目标点要写入的黑板键。
	UPROPERTY(EditAnywhere, Category="Boss|Patrol")
	FName PatrolLocationKey = TEXT("PatrolLocation");

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp,uint8* NodeMemory) override;
};
