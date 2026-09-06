// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "ANS_InvincibleWindow.generated.h"

/**
 * 无敌窗口（Invincible Window）。
 * 放在闪避蒙太奇上：Begin 给角色加 DK.Status.Invincible，End 移除。
 */
UCLASS(meta = (DisplayName = "Invincible Window"))
class FIRST_API UANS_InvincibleWindow : public UAnimNotifyState
{
	GENERATED_BODY()
	
public:
	virtual void NotifyBegin(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		float TotalDuration,
		const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	
};
