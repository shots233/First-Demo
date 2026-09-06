// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "UBTService_UpdateCombatDistance.generated.h"

/**
 * 每帧（按 Interval 节流）计算 BOSS 与当前目标的距离，
 * 并把结果写入黑板：
 *   bInAlertRange  = 距离 <= AlertRadius
 *   bInAttackRange = 距离 <= AttackRadius
 *
 * 同时处理目标死亡和 NavMesh 可达性：不可达时结束战斗注视，
 * 但保留目标弱状态以便返回 NavMesh 后自动重新警戒。
 */
UCLASS()
class FIRST_API UBTService_UpdateCombatDistance : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_UpdateCombatDistance();

	// 攻击范围：玩家进入后 BOSS 进入攻击分支（本册占位）。
	UPROPERTY(EditAnywhere, Category="Boss|CombatRange", meta=(ClampMin="0.0"))
	float AttackRadius = 250.f;

	// 警戒范围：玩家进入后 BOSS 面向玩家并开始对峙计时。
	UPROPERTY(EditAnywhere, Category="Boss|CombatRange", meta=(ClampMin="0.0"))
	float AlertRadius = 800.f;

	// 周旋距离（D28）：中间距离 → 侧移。
	UPROPERTY(EditAnywhere, Category="Boss|CombatRange", meta=(ClampMin="0.0"))
	float StrafeRadius = 500.f;

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp,uint8* NodeMemory,float DeltaSeconds) override;
};
