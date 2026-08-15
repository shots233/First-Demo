// Fill out your copyright notice in the Description page of Project Settings.


#include "Notifies/FirstANS_WeaponCollision.h"

#include "Character/DKCharacter.h"
#include "Components/Combat/DKCombatComponent.h"

void UFirstANS_WeaponCollision::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                                            float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	if (ADKCharacter* DKCharacter = MeshComp? Cast<ADKCharacter>(MeshComp->GetOwner()) : nullptr)
	{
		if (UDKCombatComponent* CombatComponent = DKCharacter->GetDKCombatComponent())
		{
			// Begin 对应挥剑有效帧开始：启用 QueryOnly 碰撞盒。
			CombatComponent->ToggleWeaponCollision(true);
		}
	}
}

void UFirstANS_WeaponCollision::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	
	if (ADKCharacter* DKCharacter = MeshComp ? Cast<ADKCharacter>(MeshComp->GetOwner()) : nullptr)
	{
		if (UDKCombatComponent* CombatComponent = DKCharacter->GetDKCombatComponent())
		{
			// End 对应有效帧结束：关闭碰撞并清除本次命中名单。
			CombatComponent->ToggleWeaponCollision(false);
		}
	}
	
}
