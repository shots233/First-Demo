// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_BossActivateAbilityByTag.generated.h"

/**
 * 读取黑板 AttackTag，调用 TryActivateAbilitiesByTag 激活对应能力。
 */
UCLASS()
class FIRST_API UBTTask_BossActivateAbilityByTag : public UBTTaskNode
{
	GENERATED_BODY()
public:
	UBTTask_BossActivateAbilityByTag();

	UPROPERTY(EditAnywhere, Category="Boss|Attack")
	FName AttackTagKey = TEXT("AttackTag");

	virtual EBTNodeResult::Type ExecuteTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;
};
