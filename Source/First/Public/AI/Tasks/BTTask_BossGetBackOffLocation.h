// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_BossGetBackOffLocation.generated.h"

/**
 * 计算远离玩家的后退点（落点距离玩家 = 周旋距离），写入黑板 BackOffLocation。
 */
UCLASS()
class FIRST_API UBTTask_BossGetBackOffLocation : public UBTTaskNode
{
	GENERATED_BODY()
	
public:
	UBTTask_BossGetBackOffLocation();

	UPROPERTY(EditAnywhere, Category="Boss|BackOff")
	FName BackOffLocationKey = TEXT("BackOffLocation");

	virtual EBTNodeResult::Type ExecuteTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;
};
