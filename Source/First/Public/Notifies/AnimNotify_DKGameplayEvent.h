// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_DKGameplayEvent.generated.h"

/**
 * 
 */
UCLASS(meta = (DisplayName = "DK Gameplay Event"))
class FIRST_API UAnimNotify_DKGameplayEvent : public UAnimNotify
{
	GENERATED_BODY()
public:
	// 在 Montage 的 Notify 面板中选择这次通知应发送哪一个 Gameplay Tag。
	UPROPERTY(EditAnywhere, Category="Event")
	FGameplayTag EventTag;
	
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
};
