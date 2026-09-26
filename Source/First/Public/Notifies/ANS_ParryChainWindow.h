// Fill in your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "ANS_ParryChainWindow.generated.h"


/**
 * 弹反连锁窗口（Parry Chain Window）。
 * 放在玩家 GuardParry 蒙太奇的 Parry 段后半（反击挥出之后、收势之前）：
 * 只向动画所属角色发送窗口开/关事件，由 GuardParry GA 保存窗口状态、
 * 接收按键并消费提前输入，跳回 Start 段重开一轮弹反。
 * 通知对象属于动画资产，可被多个角色共用，因此不保存运行时状态。
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
};
