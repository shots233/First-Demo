// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "UBTService_UpdateMoveSpeed.generated.h"

/**
 * 每帧（按 Interval 节流）根据黑板键 bShouldChase 切换 BOSS 移动速度：
 *   巡逻（bShouldChase == false）→ PatrolMoveSpeed（250，Walk）
 *   追击（bShouldChase == true） → ChaseMoveSpeed（500，Run）
 */
UCLASS()
class FIRST_API UBTService_UpdateMoveSpeed : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_UpdateMoveSpeed();

protected:
	virtual void TickNode(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory,
		float DeltaSeconds) override;
};
