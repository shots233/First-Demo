// Fill out your copyright notice in the Description page of Project Settings.


#include "Items/Weapons/DKWeapon.h"

// 作用：用新一次装备授予的 Handle 覆盖旧记录，避免重复装备后遗留过期 Handle。
void ADKWeapon::AssignGrantedAbilitySpecHandles(
	const TArray<FGameplayAbilitySpecHandle>& InSpecHandles)
{
	// 赋值而非 Add：一次装备对应一批明确的 Spec，避免旧 Handle 在重复装备后残留。
	GrantedAbilitySpecHandles = InSpecHandles;
}

// 作用：提供已记录的武器技能 Handle 副本，调用方可安全用于 ClearAbility。
TArray<FGameplayAbilitySpecHandle> ADKWeapon::GetGrantedAbilitySpecHandles() const
{
	return GrantedAbilitySpecHandles;
}

// 作用：丢弃已经从 ASC 移除的运行时 Handle；下一次装备会保存一批新的 Handle。
void ADKWeapon::ClearGrantedAbilitySpecHandles()
{
	GrantedAbilitySpecHandles.Reset();
}