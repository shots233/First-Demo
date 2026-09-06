// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "NativeGameplayTags.h"

namespace MyGameplayTags
{

	// 角色原生输入：这些 Tag 先由 ADKCharacter 处理，不会直接拿去匹配 AbilitySpec。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Move);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Look);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Jump);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Run);
	// 角色层接收该输入后，根据当前武器状态再选择 EquipSword 或 UnequipSword Ability Tag。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_ToggleSword);
	// 玩家锁定/解除目标：角色层原生输入，不直接激活 Ability。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_TargetLock);
	// 锁定期间向左/右切换目标：Axis1D 原生输入。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_SwitchTarget);
	
	// Ability 输入：ASC 用这些 Tag 匹配已授予 AbilitySpec 的 DynamicSpecSourceTags。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_EquipSword);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_UnequipSword);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_LightAttack_Sword);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Dodge);

	// Ability 身份：供调试、配置和以后按 Tag 查询 Ability 使用。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Ability_Spawn_Sword);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Ability_Equip_Sword);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Ability_Unequip_Sword);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Ability_Attack);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Ability_Attack_Light_Sword);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Ability_Dodge);
	// 所有可被受击/死亡中断的玩家动作共用类别。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Ability_Action);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Ability_HitReact);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Ability_Death);

	// 武器与事件：武器用 Tag 注册；命中时发送事件而不直接在碰撞回调里扣血。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Weapon_Sword);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Event_MeleeHit);
	// 连击窗口由 C++ AnimNotifyState 发送开/关事件，攻击 Ability 据此决定何时接收下一次输入。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Event_ComboWindow_Open);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Event_ComboWindow_Close);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Event_ActionCancelWindow_Open);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Event_ActionCancelWindow_Close);
	// 新 Ability 输入。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_GuardParry);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Execute);

	// 主角防御与处决 Ability 身份。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Ability_GuardParry);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Ability_GuardBreak);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Ability_Execute);

	// 主角防御事件。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Event_GuardHit);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Event_GuardBroken);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Event_ParrySuccess);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Event_ExecutionAbortedByBoss);

	// Defending = 格挡 Ability 整体生命周期；
	// Blocking = 当前真正能抵消伤害；松开时先移除它，再播放 End。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_Defending);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_Blocking);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_ParryWindow);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_GuardBroken);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_Executing);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_StaminaRegenPaused);

	// BOSS 弹反、破韧与处决。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Ability_ParryStagger);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Ability_Executable);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Event_Parried);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Event_PoiseBroken);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Event_ExecutionStarted);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Event_ExecutionAborted);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Event_ExecutionFinished);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Event_ExecutionApplyDamage);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Event_ExecutableExpired);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Status_Executable);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Status_BeingExecuted);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Status_Executed);

	// SetByCaller：即时精力、韧性与处决伤害。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combat_SetByCaller_StaminaDelta);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combat_SetByCaller_PoiseDelta);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combat_SetByCaller_ExecutionDamage);
	
	// BOSS 事件与状态。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Event_HitReact);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Status_Staggered);
	// BOSS 招式的通用类别（以后轻/重攻击、技能都挂它，便于统一取消）。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Ability_Attack);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Ability_HitReact);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Ability_Death);
	
	// BOSS 武器与战斗流程。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Weapon_Sword);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Event_CombatStart);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Status_WeaponDrawn);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Status_Attacking);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Status_DrawSword);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Ability_DrawSword);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Ability_Attack_Normal);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Ability_Attack_ThreeCombo);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Ability_Attack_RetreatChargedSlash);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Ability_Attack_FiveCombo);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Cooldown_Attack_Normal);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Cooldown_Attack_ThreeCombo);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Cooldown_Attack_RetreatChargedSlash);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Cooldown_Attack_FiveCombo);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Event_RetreatChargedSlash_ChargeBegin);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Event_RetreatChargedSlash_Commit);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Status_AttackDirectionLocked);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_SetByCaller_CooldownDuration);
	// BOSS侧移周旋状态（以后方向混合动画可以用它切换表现）。
    FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Status_Strafing);
	// BOSS受击窗口：攻击后摇期间可被打断。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Status_HitReactWindow);
	// BOSS受击冷却：防止窗口内连续命中无限僵直。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Cooldown_HitReact);
	// BOSS霸体窗口：五连斩等招式期间由 ANS_SuperArmorWindow 开关。
	// 窗口内普通受击与弹反都不触发僵直打断；破韧与死亡不受霸体影响。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Status_Uninterruptible);
    
	//装备和解除装备事件
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Event_Weapon_AttachToHand);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Event_Weapon_AttachToBack);

	// SetByCaller 是每一次攻击都可不同的“临时参数”，例如三段连击的段数。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_SetByCaller_BaseDamage);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_SetByCaller_ComboCount);

	// 状态 Tag 用于 Ability 之间的互斥，不应代替真正的状态变量或动画逻辑。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_Attacking);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_Dodging);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_ChangingWeapon);
	// 主角普通受击硬直期间存在。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_HitReact);
	// 当前处于锁定目标模式；目标指针仍由 TargetLockComponent 持有。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_TargetLocked);
	
	// 父标签：用于调试或以后查询“当前动作是否允许任意取消”。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_Action_Cancelable);
	// 子标签：具体哪些行为可取消当前动作。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_Action_Cancelable_By_Dodge);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_Action_Cancelable_By_Parry);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_Action_Cancelable_By_Skill);
	
	// 无敌帧：只在闪避动画的"无敌窗口"内存在，由 Notify 开关。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_Invincible);
	
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Shared_Status_Dead);
	// 任意角色真正受到生命伤害后的通用事件。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Shared_Event_DamageReceived);
}
