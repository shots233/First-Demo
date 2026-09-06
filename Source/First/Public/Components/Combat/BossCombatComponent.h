// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/Combat/FirstCombatComponent.h"
#include "BossCombatComponent.generated.h"

/*
 * BOSS 战斗组件。
 * 武器命中目标时去重，并把“谁打中了谁”以 DK.Event.MeleeHit 发给 BOSS，
 * 由正在执行的攻击能力结算伤害（和主角攻击同一条链路）。
 */
UCLASS()
class FIRST_API UBossCombatComponent : public UFirstCombatComponent
{
	GENERATED_BODY()
public:
	virtual void OnHitTargetActor(AActor* HitActor) override;
};
