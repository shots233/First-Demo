// Fill out your copyright notice in the Description page of Project Settings.


#include "MyGameplayTags.h"

namespace MyGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Move, "InputTag.Move");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Look, "InputTag.Look");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Jump, "InputTag.Jump");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Run, "InputTag.Run");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_ToggleSword, "InputTag.ToggleSword");
	
	UE_DEFINE_GAMEPLAY_TAG(InputTag_EquipSword, "InputTag.EquipSword");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_UnequipSword, "InputTag.UnequipSword");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_LightAttack_Sword, "InputTag.LightAttack.Sword");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Dodge, "InputTag.Dodge");

	UE_DEFINE_GAMEPLAY_TAG(DK_Ability_Spawn_Sword, "DK.Ability.Spawn.Sword");
	UE_DEFINE_GAMEPLAY_TAG(DK_Ability_Equip_Sword, "DK.Ability.Equip.Sword");
	UE_DEFINE_GAMEPLAY_TAG(DK_Ability_Unequip_Sword, "DK.Ability.Unequip.Sword");
	UE_DEFINE_GAMEPLAY_TAG(DK_Ability_Attack, "DK.Ability.Attack");
	UE_DEFINE_GAMEPLAY_TAG(DK_Ability_Attack_Light_Sword, "DK.Ability.Attack.Light.Sword");
	UE_DEFINE_GAMEPLAY_TAG(DK_Ability_Dodge, "DK.Ability.Dodge");

	UE_DEFINE_GAMEPLAY_TAG(DK_Weapon_Sword, "DK.Weapon.Sword");
	UE_DEFINE_GAMEPLAY_TAG(DK_Event_MeleeHit, "DK.Event.MeleeHit");
	UE_DEFINE_GAMEPLAY_TAG(DK_Event_ComboWindow_Open, "DK.Event.ComboWindow.Open");
	UE_DEFINE_GAMEPLAY_TAG(DK_Event_ComboWindow_Close, "DK.Event.ComboWindow.Close");
	UE_DEFINE_GAMEPLAY_TAG(DK_Event_ActionCancelWindow_Open,"DK.Event.ActionCancelWindow.Open");
	UE_DEFINE_GAMEPLAY_TAG(DK_Event_ActionCancelWindow_Close,"DK.Event.ActionCancelWindow.Close");
	UE_DEFINE_GAMEPLAY_TAG(DK_Event_Weapon_AttachToHand,"DK.Event.Weapon.AttachToHand");
	UE_DEFINE_GAMEPLAY_TAG(DK_Event_Weapon_AttachToBack,"DK.Event.Weapon.AttachToBack");

	UE_DEFINE_GAMEPLAY_TAG(DK_SetByCaller_BaseDamage, "DK.SetByCaller.BaseDamage");
	UE_DEFINE_GAMEPLAY_TAG(DK_SetByCaller_ComboCount, "DK.SetByCaller.ComboCount");

	UE_DEFINE_GAMEPLAY_TAG(DK_Status_Attacking, "DK.Status.Attacking");
	UE_DEFINE_GAMEPLAY_TAG(DK_Status_Dodging, "DK.Status.Dodging");
	UE_DEFINE_GAMEPLAY_TAG(DK_Status_ChangingWeapon, "DK.Status.ChangingWeapon");
	UE_DEFINE_GAMEPLAY_TAG(DK_Status_Action_Cancelable,"DK.Status.Action.Cancelable");
	UE_DEFINE_GAMEPLAY_TAG(DK_Status_Action_Cancelable_By_Dodge,"DK.Status.Action.Cancelable.By.Dodge");
	UE_DEFINE_GAMEPLAY_TAG(DK_Status_Action_Cancelable_By_Parry,"DK.Status.Action.Cancelable.By.Parry");
	UE_DEFINE_GAMEPLAY_TAG(DK_Status_Action_Cancelable_By_Skill,"DK.Status.Action.Cancelable.By.Skill");
	UE_DEFINE_GAMEPLAY_TAG(Shared_Status_Dead, "Shared.Status.Dead");
}
