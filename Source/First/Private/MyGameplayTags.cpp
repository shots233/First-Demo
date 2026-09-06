// Fill out your copyright notice in the Description page of Project Settings.


#include "MyGameplayTags.h"

namespace MyGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Move, "InputTag.Move");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Look, "InputTag.Look");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Jump, "InputTag.Jump");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Run, "InputTag.Run");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_ToggleSword, "InputTag.ToggleSword");
	
	UE_DEFINE_GAMEPLAY_TAG(InputTag_TargetLock, "InputTag.TargetLock");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_SwitchTarget, "InputTag.SwitchTarget");
	
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
	UE_DEFINE_GAMEPLAY_TAG(DK_Ability_Action, "DK.Ability.Action");
	UE_DEFINE_GAMEPLAY_TAG(DK_Ability_HitReact, "DK.Ability.HitReact");
	UE_DEFINE_GAMEPLAY_TAG(DK_Ability_Death, "DK.Ability.Death");

	UE_DEFINE_GAMEPLAY_TAG(DK_Weapon_Sword, "DK.Weapon.Sword");
	UE_DEFINE_GAMEPLAY_TAG(DK_Event_MeleeHit, "DK.Event.MeleeHit");
	UE_DEFINE_GAMEPLAY_TAG(DK_Event_ComboWindow_Open, "DK.Event.ComboWindow.Open");
	UE_DEFINE_GAMEPLAY_TAG(DK_Event_ComboWindow_Close, "DK.Event.ComboWindow.Close");
	UE_DEFINE_GAMEPLAY_TAG(DK_Event_ActionCancelWindow_Open,"DK.Event.ActionCancelWindow.Open");
	UE_DEFINE_GAMEPLAY_TAG(DK_Event_ActionCancelWindow_Close,"DK.Event.ActionCancelWindow.Close");
	
	UE_DEFINE_GAMEPLAY_TAG(InputTag_GuardParry, "InputTag.GuardParry");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Execute, "InputTag.Execute");

	UE_DEFINE_GAMEPLAY_TAG(DK_Ability_GuardParry, "DK.Ability.GuardParry");
	UE_DEFINE_GAMEPLAY_TAG(DK_Ability_GuardBreak, "DK.Ability.GuardBreak");
	UE_DEFINE_GAMEPLAY_TAG(DK_Ability_Execute, "DK.Ability.Execute");

	UE_DEFINE_GAMEPLAY_TAG(DK_Event_GuardHit, "DK.Event.GuardHit");
	UE_DEFINE_GAMEPLAY_TAG(DK_Event_GuardBroken, "DK.Event.GuardBroken");
	UE_DEFINE_GAMEPLAY_TAG(DK_Event_ParrySuccess, "DK.Event.ParrySuccess");
	UE_DEFINE_GAMEPLAY_TAG(DK_Event_ExecutionAbortedByBoss, "DK.Event.ExecutionAbortedByBoss");

	UE_DEFINE_GAMEPLAY_TAG(DK_Status_Defending, "DK.Status.Defending");
	UE_DEFINE_GAMEPLAY_TAG(DK_Status_Blocking, "DK.Status.Blocking");
	UE_DEFINE_GAMEPLAY_TAG(DK_Status_ParryWindow, "DK.Status.ParryWindow");
	UE_DEFINE_GAMEPLAY_TAG(DK_Status_GuardBroken, "DK.Status.GuardBroken");
	UE_DEFINE_GAMEPLAY_TAG(DK_Status_Executing, "DK.Status.Executing");
	UE_DEFINE_GAMEPLAY_TAG(DK_Status_StaminaRegenPaused, "DK.Status.StaminaRegenPaused");

	UE_DEFINE_GAMEPLAY_TAG(Boss_Ability_ParryStagger, "Boss.Ability.ParryStagger");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Ability_Executable, "Boss.Ability.Executable");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Event_Parried, "Boss.Event.Parried");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Event_PoiseBroken, "Boss.Event.PoiseBroken");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Event_ExecutionStarted, "Boss.Event.ExecutionStarted");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Event_ExecutionAborted, "Boss.Event.ExecutionAborted");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Event_ExecutionFinished, "Boss.Event.ExecutionFinished");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Event_ExecutionApplyDamage, "Boss.Event.Execution.ApplyDamage");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Event_ExecutableExpired, "Boss.Event.ExecutableExpired");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Status_Executable, "Boss.Status.Executable");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Status_BeingExecuted, "Boss.Status.BeingExecuted");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Status_Executed, "Boss.Status.Executed");

	UE_DEFINE_GAMEPLAY_TAG(Combat_SetByCaller_StaminaDelta, "Combat.SetByCaller.StaminaDelta");
	UE_DEFINE_GAMEPLAY_TAG(Combat_SetByCaller_PoiseDelta, "Combat.SetByCaller.PoiseDelta");
	UE_DEFINE_GAMEPLAY_TAG(Combat_SetByCaller_ExecutionDamage, "Combat.SetByCaller.ExecutionDamage");
	
	UE_DEFINE_GAMEPLAY_TAG(Boss_Event_HitReact, "Boss.Event.HitReact");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Status_Staggered, "Boss.Status.Staggered");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Ability_Attack, "Boss.Ability.Attack");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Ability_HitReact, "Boss.Ability.HitReact");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Ability_Death, "Boss.Ability.Death");
	
	UE_DEFINE_GAMEPLAY_TAG(Boss_Weapon_Sword, "Boss.Weapon.Sword");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Event_CombatStart, "Boss.Event.CombatStart");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Status_WeaponDrawn, "Boss.Status.WeaponDrawn");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Status_Attacking, "Boss.Status.Attacking");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Status_DrawSword, "Boss.Status.DrawSword");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Ability_DrawSword, "Boss.Ability.DrawSword");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Ability_Attack_Normal, "Boss.Ability.Attack.Normal");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Ability_Attack_ThreeCombo, "Boss.Ability.Attack.ThreeCombo");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Ability_Attack_RetreatChargedSlash, "Boss.Ability.Attack.RetreatChargedSlash");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Cooldown_Attack_Normal, "Boss.Cooldown.Attack.Normal");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Cooldown_Attack_ThreeCombo, "Boss.Cooldown.Attack.ThreeCombo");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Cooldown_Attack_RetreatChargedSlash, "Boss.Cooldown.Attack.RetreatChargedSlash");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Event_RetreatChargedSlash_ChargeBegin, "Boss.Event.RetreatChargedSlash.ChargeBegin");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Event_RetreatChargedSlash_Commit, "Boss.Event.RetreatChargedSlash.Commit");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Status_AttackDirectionLocked, "Boss.Status.AttackDirectionLocked");
	UE_DEFINE_GAMEPLAY_TAG(Boss_SetByCaller_CooldownDuration, "Boss.SetByCaller.CooldownDuration");
	
	UE_DEFINE_GAMEPLAY_TAG(Boss_Status_Strafing, "Boss.Status.Strafing");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Status_HitReactWindow, "Boss.Status.HitReactWindow");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Cooldown_HitReact, "Boss.Cooldown.HitReact");
	
	UE_DEFINE_GAMEPLAY_TAG(DK_Event_Weapon_AttachToHand,"DK.Event.Weapon.AttachToHand");
	UE_DEFINE_GAMEPLAY_TAG(DK_Event_Weapon_AttachToBack,"DK.Event.Weapon.AttachToBack");

	UE_DEFINE_GAMEPLAY_TAG(DK_SetByCaller_BaseDamage, "DK.SetByCaller.BaseDamage");
	UE_DEFINE_GAMEPLAY_TAG(DK_SetByCaller_ComboCount, "DK.SetByCaller.ComboCount");

	UE_DEFINE_GAMEPLAY_TAG(DK_Status_Attacking, "DK.Status.Attacking");
	UE_DEFINE_GAMEPLAY_TAG(DK_Status_Dodging, "DK.Status.Dodging");
	
	UE_DEFINE_GAMEPLAY_TAG(DK_Status_TargetLocked,"DK.Status.TargetLocked");
	
	UE_DEFINE_GAMEPLAY_TAG(DK_Status_ChangingWeapon, "DK.Status.ChangingWeapon");
	UE_DEFINE_GAMEPLAY_TAG(DK_Status_HitReact, "DK.Status.HitReact");
	UE_DEFINE_GAMEPLAY_TAG(DK_Status_Action_Cancelable,"DK.Status.Action.Cancelable");
	UE_DEFINE_GAMEPLAY_TAG(DK_Status_Action_Cancelable_By_Dodge,"DK.Status.Action.Cancelable.By.Dodge");
	UE_DEFINE_GAMEPLAY_TAG(DK_Status_Action_Cancelable_By_Parry,"DK.Status.Action.Cancelable.By.Parry");
	UE_DEFINE_GAMEPLAY_TAG(DK_Status_Action_Cancelable_By_Skill,"DK.Status.Action.Cancelable.By.Skill");
	
	UE_DEFINE_GAMEPLAY_TAG(DK_Status_Invincible, "DK.Status.Invincible");
	
	UE_DEFINE_GAMEPLAY_TAG(Shared_Status_Dead, "Shared.Status.Dead");
	UE_DEFINE_GAMEPLAY_TAG(Shared_Event_DamageReceived, "Shared.Event.DamageReceived");
}
