// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "ANS_SuperArmorWindow.generated.h"


/**
 * 霸体窗口（Super Armor Window）。
 * 放在 BOSS 霸体招式（如四连斩）的挥砍段：窗口内挂 Boss.Status.Uninterruptible，
 * 普通受击（GA_Boss_HitReact 被阻挡）与弹反（GA_Boss_ParryStagger 走霸体分支：
 * 照常削韧但不僵直、不打断）都无法中断招式；破韧与死亡不受霸体影响。
 * 后摇惩罚窗口（ANS_HitReactWindow）应与本窗口错开摆放，不要重叠。
 */
UCLASS(meta = (DisplayName = "Super Armor Window"))
class FIRST_API UANS_SuperArmorWindow : public UAnimNotifyState
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
	// 避免死亡门控跳过 Begin 后 End 仍无条件移除导致 Loose Tag 计数为负
	//（ANS_InvincibleWindow 双重移除问题的规避写法）。
	bool bTagAddedByThisWindow = false;
};
