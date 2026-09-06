// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "ANS_ActionCancel.generated.h"

/**
 * 
 */
UCLASS(meta = (DisplayName = "Action Cancel Window"))
class FIRST_API UANS_ActionCancel : public UAnimNotifyState
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category="Action Cancel")
	FGameplayTagContainer GrantedCancelTags;
	
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,const FAnimNotifyEventReference& EventReference) override;

};
