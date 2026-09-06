#include "Notifies/ANS_WeaponTelegraph.h"

#include "Character/BaseCharacter.h"
#include "Components/Combat/FirstCombatComponent.h"
#include "Items/Weapons/FirstWeaponBase.h"

namespace
{
	AFirstWeaponBase* GetCurrentWeapon(const USkeletalMeshComponent* MeshComp)
	{
		const ABaseCharacter* Character = MeshComp
			? Cast<ABaseCharacter>(MeshComp->GetOwner())
			: nullptr;
		const UFirstCombatComponent* Combat = Character
			? Character->FindComponentByClass<UFirstCombatComponent>()
			: nullptr;
		return Combat ? Combat->GetCharacterCurrentEquippedWeapon() : nullptr;
	}
}

void UANS_WeaponTelegraph::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (AFirstWeaponBase* Weapon = GetCurrentWeapon(MeshComp))
	{
		Weapon->SetWeaponTelegraphEnabled(true);
	}
}

void UANS_WeaponTelegraph::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (AFirstWeaponBase* Weapon = GetCurrentWeapon(MeshComp))
	{
		Weapon->SetWeaponTelegraphEnabled(false);
	}
}
