// Fill out your copyright notice in the Description page of Project Settings.


#include "Notifies/AnimNotify_DKGameplayEvent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbilityTypes.h"

void UAnimNotify_DKGameplayEvent::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                                         const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	
	AActor* OwnerActor = MeshComp ? MeshComp->GetOwner() : nullptr;
	if (!OwnerActor||!EventTag.IsValid())
	{
		return;
	}
	
	FGameplayEventData EventData;
	EventData.Instigator = OwnerActor;
	EventData.Target = OwnerActor;
	
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(OwnerActor, EventTag, EventData);
	
}
