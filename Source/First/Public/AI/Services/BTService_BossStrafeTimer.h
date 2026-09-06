// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BTService_BossStrafeTimer.generated.h"

/**
 * 侧移计时器服务。
 * 挂在"战斗"分支上：只要 BOSS 处于侧移范围，就累计时间；
 * 超过 StrafeMaxDuration 后把黑板键 bStrafeTimeout 置 true，
 * 行为树据此停止侧移、转为靠近玩家（防止无限绕圈）。
 * 离开侧移范围或正在出招（bIsBusy）时计时清零。
 */
UCLASS()
class FIRST_API UBTService_BossStrafeTimer : public UBTService
{
	GENERATED_BODY()
public:
	UBTService_BossStrafeTimer();

	// 侧移最长持续时间（秒）。超时后停止侧移，转为靠近。
	UPROPERTY(EditAnywhere, Category="Boss|Strafe", meta=(ClampMin="0.5"))
	float StrafeMaxDuration = 1.5f;

protected:
	// 每个 BOSS 实例的计时数据放在 NodeMemory 里，避免多个 BOSS 共用同一份计时。
	virtual uint16 GetInstanceMemorySize() const override;

	virtual void TickNode(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory,
		float DeltaSeconds) override;
};
