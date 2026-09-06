// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "GameplayTagContainer.h"
#include "BTService_UpdateBossState.generated.h"

/**
 * 
 */
UCLASS()
class FIRST_API UBTService_UpdateBossState : public UBTService
{
	GENERATED_BODY()
public:
	UBTService_UpdateBossState();

	// 行为锁定标签：只要 ASC 上存在其中任意一个，bIsBusy 就为 true，
	// 追击/巡逻分支会被挡住（攻击中、拔剑中、以后的僵直等都往这里加）。
	// 可在行为树节点详情里直接增删标签，无需改代码。
	UPROPERTY(EditAnywhere, Category="Boss|State")
	FGameplayTagContainer ActionBlockingTags;

	// 处决接管标签：存在任意一个时旋转完全冻结（Frozen），
	// 朝向交给处决蒙太奇表现，AI 不得用 Focus 持续把 BOSS 扭向玩家。
	// 口径与 BTTask_BossDamageAlert 的 bExecutionControlled 一致。
	UPROPERTY(EditAnywhere, Category="Boss|State")
	FGameplayTagContainer ExecutionControlTags;
	
	
	
protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	
	
};
