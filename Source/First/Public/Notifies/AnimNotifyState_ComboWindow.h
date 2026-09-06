// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_ComboWindow.generated.h"

/**
 * 
 */
UCLASS()
class FIRST_API UAnimNotifyState_ComboWindow : public UAnimNotifyState
{
	GENERATED_BODY()
	
public:
	// 作用：连击输入窗口开始时，向当前角色发送 DK.Event.ComboWindow.Open。
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp,UAnimSequenceBase* Animation,float TotalDuration,const FAnimNotifyEventReference& EventReference) override;

	// 作用：连击输入窗口结束时发送 Close，让攻击 Ability 停止等待下一次按键。
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp,UAnimSequenceBase* Animation,const FAnimNotifyEventReference& EventReference) override;
};
