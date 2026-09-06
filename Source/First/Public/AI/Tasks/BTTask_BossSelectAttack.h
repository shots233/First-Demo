// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_BossSelectAttack.generated.h"

/**
 * 
 */

USTRUCT(BlueprintType)
struct FBossAttackOption
{
	GENERATED_BODY()

	// 招式的身份标签（AbilityTags 里的那个）。
	UPROPERTY(EditAnywhere, Category="Boss|Attack")
	FGameplayTag AbilityTag;

	// 该招式的冷却标签（冷却中不会选中）。
	UPROPERTY(EditAnywhere, Category="Boss|Attack")
	FGameplayTag CooldownTag;

	// 最大使用距离（超过该距离不选）。
	UPROPERTY(EditAnywhere, Category="Boss|Attack", meta=(ClampMin="0.0"))
	float MaxRange = 300.f;

	// 最小使用距离（低于该距离不选）。
	UPROPERTY(EditAnywhere, Category="Boss|Attack", meta=(ClampMin="0.0"))
	float MinRange = 0.f;

	// 候选招式间的相对权重；0 表示禁用该选项。
	UPROPERTY(EditAnywhere, Category="Boss|Attack", meta=(ClampMin="0.0"))
	float SelectionWeight = 1.f;

	// 0 表示无需检查；正数表示选择前必须保证身后至少有该距离的安全空间。
	UPROPERTY(EditAnywhere, Category="Boss|Attack", meta=(ClampMin="0.0", Units="cm"))
	float RequiredRetreatSpace = 0.f;
};
UCLASS()
class FIRST_API UBTTask_BossSelectAttack : public UBTTaskNode
{
	GENERATED_BODY()
public:
	UBTTask_BossSelectAttack();

	// 在行为树节点详情里配置招式表。
	UPROPERTY(EditAnywhere, Category="Boss|Attack")
	TArray<FBossAttackOption> AttackOptions;

	UPROPERTY(EditAnywhere, Category="Boss|Attack")
	FName AttackTagKey = TEXT("AttackTag");

	virtual EBTNodeResult::Type ExecuteTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;
};
