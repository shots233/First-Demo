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

	// 武器与事件：武器用 Tag 注册；命中时发送事件而不直接在碰撞回调里扣血。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Weapon_Sword);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Event_MeleeHit);
	// 连击窗口由 C++ AnimNotifyState 发送开/关事件，攻击 Ability 据此决定何时接收下一次输入。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Event_ComboWindow_Open);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Event_ComboWindow_Close);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Event_ActionCancelWindow_Open);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Event_ActionCancelWindow_Close);
	
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
	// 父标签：用于调试或以后查询“当前动作是否允许任意取消”。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_Action_Cancelable);
	// 子标签：具体哪些行为可取消当前动作。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_Action_Cancelable_By_Dodge);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_Action_Cancelable_By_Parry);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_Action_Cancelable_By_Skill);
	
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Shared_Status_Dead);
}
