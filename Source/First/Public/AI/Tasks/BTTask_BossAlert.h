// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_BossAlert.generated.h"

/**
 * 警戒/对峙任务。
 *
 * 行为：
 * 1. 面向玩家（每帧旋转到玩家方向）；
 * 2. 保持对峙 StareDuration 秒；
 * 3. 若对峙期间玩家离开警戒范围或目标丢失 → 任务失败（回巡逻）；
 * 4. 对峙完成 → 把 bShouldChase 置 true（触发追击）。
 * 5. 若武器已经拔出，说明入战仪式已经完成，立即跳过对峙并恢复战斗。
 */
UCLASS()
class FIRST_API UBTTask_BossAlert : public UBTTaskNode
{
	GENERATED_BODY()
public:
	UBTTask_BossAlert();

	// 对峙时长：进入警戒范围后看玩家多久才发起追击。
	UPROPERTY(EditAnywhere, Category="Boss|Alert", meta=(ClampMin="0.0"))
	float StareDuration = 3.f;

	// 警戒期间是否持续旋转面向玩家（关闭则只对峙不转头）。
	UPROPERTY(EditAnywhere, Category="Boss|Alert")
	bool bRotateToFaceTarget = true;
	
protected:
	virtual uint16 GetInstanceMemorySize() const override;
	
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp,uint8* NodeMemory) override;
	
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp,uint8* NodeMemory,float DeltaSeconds) override;
};
