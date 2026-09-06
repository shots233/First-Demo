// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "ANS_SlashTrail.generated.h"

/**
 * 武器刀光拖影窗口。
 * 只能跟在攻击蒙太奇里：进入时开启当前装备武器的拖影，离开时关闭。
 * 本通知不参与战斗判定，与 FirstANS_WeaponCollision（伤害窗口）相互独立。
 */
UCLASS(meta = (DisplayName = "Slash Trail"))
class FIRST_API UANS_SlashTrail : public UAnimNotifyState
{
	GENERATED_BODY()
public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		float TotalDuration, const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};
