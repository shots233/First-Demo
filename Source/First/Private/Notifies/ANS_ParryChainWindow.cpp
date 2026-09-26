// Fill in your copyright notice in the Description page of Project Settings.


#include "Notifies/ANS_ParryChainWindow.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "MyGameplayTags.h"
#include "Character/BaseCharacter.h"

namespace
{
	void SendParryChainWindowEvent(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		UANS_ParryChainWindow* NotifyState, const FGameplayTag& EventTag)
	{
		ABaseCharacter* Character = MeshComp
			? Cast<ABaseCharacter>(MeshComp->GetOwner())
			: nullptr;
		if (!Character || !Character->GetAbilitySystemComponent())
		{
			return;
		}

		FGameplayEventData EventData;
		EventData.EventTag = EventTag;
		EventData.Instigator = Character;
		EventData.Target = Character;
		// GA 使用通知对象与动画资产识别窗口来源，并自行管理标签和清理。
		EventData.OptionalObject = NotifyState;
		EventData.OptionalObject2 = Animation;
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Character, EventTag, EventData);
	}
}

void UANS_ParryChainWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	SendParryChainWindowEvent(MeshComp, Animation, this, MyGameplayTags::DK_Event_ParryChainWindow_Open);
}

void UANS_ParryChainWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	SendParryChainWindowEvent(MeshComp, Animation, this, MyGameplayTags::DK_Event_ParryChainWindow_Close);
}
