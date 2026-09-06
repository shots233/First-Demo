#include "Notifies/ANS_SlashTrail.h"

#include "Character/BaseCharacter.h"
#include "Components/Combat/FirstCombatComponent.h"
#include "Items/Weapons/FirstWeaponBase.h"

void UANS_SlashTrail::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	ABaseCharacter* Character = MeshComp? Cast<ABaseCharacter>(MeshComp->GetOwner()): nullptr;
	if (!Character)
	{
		return;
	}

	UFirstCombatComponent* Combat =Character->FindComponentByClass<UFirstCombatComponent>();
	if (!Combat)
	{
		return;
	}

	if (AFirstWeaponBase* Weapon = Combat->GetCharacterCurrentEquippedWeapon())
	{
		Weapon->SetSlashTrailEnabled(true);
	}
}

void UANS_SlashTrail::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	ABaseCharacter* Character = MeshComp? Cast<ABaseCharacter>(MeshComp->GetOwner()): nullptr;
	if (!Character)
	{
		return;
	}

	UFirstCombatComponent* Combat =Character->FindComponentByClass<UFirstCombatComponent>();
	if (!Combat)
	{
		return;
	}

	if (AFirstWeaponBase* Weapon = Combat->GetCharacterCurrentEquippedWeapon())
	{
		Weapon->SetSlashTrailEnabled(false);
	}
}