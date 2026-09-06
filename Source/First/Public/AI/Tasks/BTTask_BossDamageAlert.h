#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_BossDamageAlert.generated.h"

/**
 * 受伤强制入战：停止移动，在固定时间内平滑面向攻击者，
 * 随后触发现有拔剑事件并把控制权交给战斗分支。
 */
UCLASS()
class FIRST_API UBTTask_BossDamageAlert : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_BossDamageAlert();

	UPROPERTY(EditAnywhere, Category="Boss|Damage Alert",
		meta=(ClampMin="0.0", UIMin="0.0"))
	float TurnDuration = 0.25f;

	UPROPERTY(EditAnywhere, Category="Boss|Damage Alert",
		meta=(ClampMin="0.0", ClampMax="45.0"))
	float TurnToleranceDegrees = 2.f;

	// 防止拔剑 Montage 循环或状态标签残留时永久占住行为树最高优先级分支。
	// 设为 0 可关闭超时保护。
	UPROPERTY(EditAnywhere, Category="Boss|Damage Alert",
		meta=(ClampMin="0.0", UIMin="0.0", Units="s"))
	float DrawSwordTimeout = 5.f;

protected:
	virtual uint16 GetInstanceMemorySize() const override;

	virtual EBTNodeResult::Type ExecuteTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;

	virtual EBTNodeResult::Type AbortTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;

	virtual void TickTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory,
		float DeltaSeconds) override;
};
