// Fill out your copyright notice in the Description page of Project Settings.


#include "Notifies/ANS_InvincibleWindow.h"

#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "Character/BaseCharacter.h"
#include "MyGameplayTags.h"

void UANS_InvincibleWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	ABaseCharacter* Character = MeshComp
		? Cast<ABaseCharacter>(MeshComp->GetOwner())
		: nullptr;
	UFirstAbilitySystemComponent* ASC = Character
		? Character->GetFirstAbilitySystemComponent()
		: nullptr;

	if (!ASC)
	{
		return;
	}

	// 死亡后不再进入无敌。
	if (ASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead))
	{
		return;
	}

	ASC->AddLooseGameplayTag(MyGameplayTags::DK_Status_Invincible);
}

void UANS_InvincibleWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	ABaseCharacter* Character = MeshComp
		? Cast<ABaseCharacter>(MeshComp->GetOwner())
		: nullptr;
	UFirstAbilitySystemComponent* ASC = Character
		? Character->GetFirstAbilitySystemComponent()
		: nullptr;

	if (!ASC)
	{
		return;
	}

	ASC->RemoveLooseGameplayTag(MyGameplayTags::DK_Status_Invincible);
}
