// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "ANS_HitReactWindow.generated.h"


/**
 * 受击窗口（Hit React Window）。
 * 放在 BOSS 攻击蒙太奇的后摇段：窗口内受击才会打断（配合 GA_Boss_HitReact 的 RequiredTags）。
 */
UCLASS(meta = (DisplayName = "Hit React Window"))
class FIRST_API UANS_HitReactWindow : public UAnimNotifyState
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
