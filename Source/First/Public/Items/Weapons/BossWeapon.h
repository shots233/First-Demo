// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Items/Weapons/FirstWeaponBase.h"
#include "BossWeapon.generated.h"

/**
 * BOSS 的剑。所有数据（网格体、碰撞盒大小、挂点）在 BP_BossWeapon 里配置。
 */
UCLASS()
class FIRST_API ABossWeapon : public AFirstWeaponBase
{
	GENERATED_BODY()
	
};
