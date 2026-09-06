// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "FirstANS_WeaponCollision.generated.h"

/**
 * 
 */
UCLASS()
class FIRST_API UFirstANS_WeaponCollision : public UAnimNotifyState
{
	GENERATED_BODY()
public:
	// 作用：动画通知区间开始时打开武器碰撞，对应剑刃真正具有伤害的第一帧。
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, 
		UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	
	// 作用：动画通知区间结束时关闭武器碰撞，并让 CombatComponent 重置本段攻击命中名单。
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, 
		UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	
};
