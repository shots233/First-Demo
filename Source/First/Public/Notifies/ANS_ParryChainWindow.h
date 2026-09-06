// Fill in your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "ANS_ParryChainWindow.generated.h"


/**
 * 弹反连锁窗口（Parry Chain Window）。
 * 放在玩家 GuardParry 蒙太奇的 Parry 段后半（反击挥出之后、收势之前）：
 * 窗口内挂 DK.Status.ParryChainWindow，角色层把这段时间内的弹反按键
 * 转成 DK_Event_ParryChainRequest，GA 据此跳回 Start 段并重开 0.15s 弹反窗口，
 * 实现"对着 BOSS 连段连续弹反"。窗口外按弹反维持原行为（被 Defending 标签挡住）。
 */
UCLASS(meta = (DisplayName = "Parry Chain Window"))
class FIRST_API UANS_ParryChainWindow : public UAnimNotifyState
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

private:
	// 记录本窗口是否真的加过标签：NotifyEnd 只在加过时才移除，
	// 避免死亡门控跳过 Begin 后 End 仍移除导致 Loose Tag 计数为负。
	bool bTagAddedByThisWindow = false;
};
