# BOSS 武器与攻击系统实现指导（第一册：背剑、拔剑、普通攻击、三连击）

> 适用项目：`First`（UE 5.6 **中文版** / C++ / GAS / 内置 AI）
> 前置条件：BOSS 受击与死亡已完成；主角攻击能对 BOSS 造成伤害
> 本册目标：BOSS 出生背剑 → 发现玩家后拔剑到手 → 进攻击范围后随机释放**普通攻击**或**三连击**（三连击算一个技能），带冷却

---

## 0. 先看结论（本册完成后得到什么）

```text
BOSS 出生        → 剑生成在背上（背部挂点）
首次发现玩家     → 播放拔剑动画，剑挂到手上，添加 Boss.Status.WeaponDrawn
进入攻击范围     → 行为树选招：普通攻击（单段）或 三连击（一段技能，三段动画顺序播放）
招式都带冷却     → 冷却中不会被选中；受击/死亡时不会放招
```

---

## 1. 设计说明（先读）

### 1.1 本册招式表

| 技能 | 表现 | 冷却 | 使用距离 |
|---|---|---|---|
| 普通攻击 | 单段挥砍（`Anim_DKF_Attack_01`） | 2.5 秒 | ≤300 |
| 三连击 | 一个技能，三段顺序播放（`Anim_DKF_Attack_01/02/03`） | 6 秒 | ≤350 |

选招规则（本册）：**冷却标签不在身上 + 距离在范围内 → 随机选一个**。特殊技能（跳跃劈砍等距离位移型）留到第二册。

### 1.2 武器体系复用

- 剑用包里自带的 `SM_DKF_Sword`，BOSS 武器类 `ABossWeapon` 继承现有的 `AFirstWeaponBase`；
- 命中检测复用 `UFirstCombatComponent` 的碰撞开关与命中去重，但需要一个 `UBossCombatComponent` 把"命中"转成 `DK.Event.MeleeHit` 事件（和玩家同一个标签）；
- ⚠️ 现有 `FirstANS_WeaponCollision` 只认玩家（`Cast<ADKCharacter>`），需要**通用化**，BOSS 的攻击蒙太奇才能用它开合武器碰撞。

### 1.3 拔剑触发时机

用**首次索敌成功**（`TargetActor` 第一次写入）作为"进入战斗"信号，向 BOSS 发 `Boss.Event.CombatStart` → `GA_Boss_DrawSword` 触发。拔剑完成后加 `Boss.Status.WeaponDrawn` 标签，攻击能力靠 `ActivationRequiredTags` 要求该标签，没拔剑就不放招。

### 1.4 冷却用 GAS 原生 Cooldown

不写自定义计时器：每个攻击 GA 配 `CooldownGameplayEffectClass`（`UFirstGE_BossCooldown`，时长用 `SetByCaller`），基类覆写 `ApplyCooldown` 写入自己的冷却秒数。冷却期间能力无法激活，选招任务过滤"冷却标签不在身上"的招式。

> ⚠️ UE 5.6：`UGameplayAbility` 的 AbilityTags / AssetTags **已合并成一个容器**（成员名 `AbilityTags`，编辑器显示名 AssetTags），统一用 `SetAssetTags()` 设置；`TryActivateAbilitiesByTag` 和 `CancelAbilities` 都匹配这个容器，**不需要（也不能）双写**。

### 1.5 UE 5.6 API 差异速查（本册代码已按此修正）

| 旧写法（会编译报错） | UE 5.6 正确写法 |
|---|---|
| `FGameplayAbilityTrigger` | `FAbilityTriggerData`（AbilityTriggers 的元素类型已改名） |
| `AbilityTags.AddTag(...)` 单独往 AbilityTags 加 | 只用 `SetAssetTags(Container)`（5.6 合并成一个容器） |
| `ApplyEffectSpecHandleToTarget(Payload.Target, ...)` | 5.6 的 `Payload.Target` 是 `const` 指针，需 `const_cast<AActor*>(Payload.Target.Get())` |
| 直接用基类 `CooldownTags` 成员 | 5.6 基类已移除 `CooldownTags`，派生类自己声明 `FGameplayTagContainer CooldownTags;` 并覆写 `GetCooldownTags()` |

---

## 2. 本册改动总览（按文件找位置）

| 文件/资产 | 路径 | 操作 | 新增/修改内容 |
|---|---|---|---|
| `MyGameplayTags.h/.cpp` | `Source/First/Public/`<br>`Source/First/Private/` | 修改 | 新增 9 个 BOSS 标签 |
| `FirstANS_WeaponCollision.cpp` | `Private/Notifies/` | 修改 | 通用化：不再只认玩家 |
| `UBossCombatComponent.h/.cpp` | `Public/Components/Combat/`<br>`Private/Components/Combat/` | 新建 | 命中去重 + 发 MeleeHit 事件 |
| `ABossWeapon.h/.cpp` | `Public/Items/Weapons/`<br>`Private/Items/Weapons/` | 新建 | BOSS 剑（数据在 BP_BossWeapon 里配） |
| `BossCharacter.h/.cpp` | `Public/Character/`<br>`Private/Character/` | 修改 | 战斗组件、武器类、挂点、蒙太奇、出生背剑 |
| `UFirstBossGameplayAbility.h/.cpp` | `Public/AbilitySystem/Abilities/Boss/`<br>`Private/AbilitySystem/Abilities/Boss/` | 新建 | BOSS 能力基类（冷却 + 伤害 Spec + 访问器） |
| `UFirstGE_BossCooldown.h/.cpp` | `Public/AbilitySystem/GameplayEffects/`<br>`Private/AbilitySystem/GameplayEffects/` | 新建 | 冷却 GE（SetByCaller 时长） |
| `UGA_Boss_DrawSword.h/.cpp` | `Public/AbilitySystem/Abilities/Boss/`<br>`Private/AbilitySystem/Abilities/Boss/` | 新建 | 拔剑能力 |
| `UGA_Boss_NormalAttack.h/.cpp` | 同上 | 新建 | 普通攻击 |
| `UGA_Boss_ThreeCombo.h/.cpp` | 同上 | 新建 | 三连击（一个技能） |
| `UBTTask_BossSelectAttack.h/.cpp` | `Public/AI/Tasks/`<br>`Private/AI/Tasks/` | 新建 | 选招任务（冷却+距离+随机） |
| `UBTTask_BossActivateAbilityByTag.h/.cpp` | 同上 | 新建 | 按标签激活能力 |
| `BossAIController.cpp` | `Private/Controller/` | 修改 | 首次索敌成功发 CombatStart 事件 |
| 资产 | `/Game/Enemy/` | 配置 | 挂点、蒙太奇、BP_BossWeapon、BP_Boss、DA、BB、BT |

---

## 3. 第一步：新增 Gameplay Tags

> 【改动位置】文件 `MyGameplayTags.h`
> 【新增内容】9 个标签声明

在 `Boss_Ability_Death` 声明附近新增：

```cpp
	// BOSS 武器与战斗流程。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Weapon_Sword);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Event_CombatStart);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Status_WeaponDrawn);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Status_Attacking);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Status_DrawSword);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Ability_DrawSword);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Ability_Attack_Normal);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Ability_Attack_ThreeCombo);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Cooldown_Attack_Normal);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Cooldown_Attack_ThreeCombo);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_SetByCaller_CooldownDuration);
```

> 【改动位置】文件 `MyGameplayTags.cpp`
> 【新增内容】对应定义

```cpp
	UE_DEFINE_GAMEPLAY_TAG(Boss_Weapon_Sword, "Boss.Weapon.Sword");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Event_CombatStart, "Boss.Event.CombatStart");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Status_WeaponDrawn, "Boss.Status.WeaponDrawn");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Status_Attacking, "Boss.Status.Attacking");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Status_DrawSword, "Boss.Status.DrawSword");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Ability_DrawSword, "Boss.Ability.DrawSword");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Ability_Attack_Normal, "Boss.Ability.Attack.Normal");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Ability_Attack_ThreeCombo, "Boss.Ability.Attack.ThreeCombo");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Cooldown_Attack_Normal, "Boss.Cooldown.Attack.Normal");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Cooldown_Attack_ThreeCombo, "Boss.Cooldown.Attack.ThreeCombo");
	UE_DEFINE_GAMEPLAY_TAG(Boss_SetByCaller_CooldownDuration, "Boss.SetByCaller.CooldownDuration");
```

编译闸门 1。

---

## 4. 第二步：通用化武器碰撞通知

> 【改动位置】文件 `FirstANS_WeaponCollision.cpp`
> 【修改内容】不再 `Cast<ADKCharacter>`，改成任意 `ABaseCharacter` + `UFirstCombatComponent`

把两个 include 换掉：

```cpp
#include "Character/BaseCharacter.h"
#include "Components/Combat/FirstCombatComponent.h"
```

`NotifyBegin` 和 `NotifyEnd` 里的判断改为：

```cpp
	if (ABaseCharacter* Character = MeshComp ? Cast<ABaseCharacter>(MeshComp->GetOwner()) : nullptr)
	{
		if (UFirstCombatComponent* CombatComponent = Character->FindComponentByClass<UFirstCombatComponent>())
		{
			CombatComponent->ToggleWeaponCollision(true);   // NotifyBegin
			// CombatComponent->ToggleWeaponCollision(false); // NotifyEnd
		}
	}
```

编译闸门 2。

---

## 5. 第三步：新建 UBossCombatComponent

用编辑器向导创建：

1. **工具（Tools）→ 新建 C++ 类（New C++ Class）** → 勾选 **显示所有类（Show All Classes）**；
2. 搜索并选择父类 **`FirstCombatComponent`**（即 `UFirstCombatComponent`）；
3. 类名填写 `BossCombatComponent`（不要带 U 前缀，向导会自动加 U，最终类名 `UBossCombatComponent`）；
4. 文件放到 `Components/Combat/`，整体替换为下面的内容（新建文件）。

> 【改动位置】新建文件 `BossCombatComponent.h` / `BossCombatComponent.cpp`
> 【新增内容】命中去重后向 BOSS 自己发 `DK.Event.MeleeHit`（和玩家同一套机制）

`BossCombatComponent.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/Combat/FirstCombatComponent.h"
#include "BossCombatComponent.generated.h"

/**
 * BOSS 战斗组件。
 * 武器命中目标时去重，并把“谁打中了谁”以 DK.Event.MeleeHit 发给 BOSS，
 * 由正在执行的攻击能力结算伤害（和主角攻击同一条链路）。
 */
UCLASS()
class FIRST_API UBossCombatComponent : public UFirstCombatComponent
{
	GENERATED_BODY()

public:
	virtual void OnHitTargetActor(AActor* HitActor) override;
};
```

`BossCombatComponent.cpp`：

```cpp
#include "Components/Combat/BossCombatComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "MyGameplayTags.h"

void UBossCombatComponent::OnHitTargetActor(AActor* HitActor)
{
	Super::OnHitTargetActor(HitActor);

	// 同一碰撞窗口内同一目标只发一次事件，避免重复结算。
	if (!HitActor || OverlappedActors.Contains(HitActor))
	{
		return;
	}

	OverlappedActors.AddUnique(HitActor);

	FGameplayEventData EventData;
	EventData.Instigator = GetOwningPawn();
	EventData.Target = HitActor;

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
		GetOwningPawn(),
		MyGameplayTags::DK_Event_MeleeHit,
		EventData);
}
```

编译闸门 3。

---

## 6. 第四步：新建 ABossWeapon

用编辑器向导创建：

1. **工具（Tools）→ 新建 C++ 类（New C++ Class）** → 勾选 **显示所有类（Show All Classes）**；
2. 搜索并选择父类 **`FirstWeaponBase`**（即 `AFirstWeaponBase`）；
3. 类名填写 `BossWeapon`（不要带 A 前缀，最终类名 `ABossWeapon`）；
4. 文件放到 `Items/Weapons/`，整体替换为下面的内容（新建文件）。

> 【改动位置】新建文件 `BossWeapon.h` / `BossWeapon.cpp`
> 【新增内容】空的 BOSS 剑类（网格体/碰撞在 BP_BossWeapon 里配）

`BossWeapon.h`：

```cpp
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
```

`BossWeapon.cpp`：

```cpp
#include "Items/Weapons/BossWeapon.h"
```

编译闸门 4。

---

## 7. 第五步：修改 ABossCharacter（战斗组件、武器、挂点、蒙太奇、出生背剑）

### 7.1 头文件

> 【改动位置】文件 `BossCharacter.h`
> 【新增内容】战斗组件、武器类、挂点名、4 个蒙太奇；`BeginPlay` 里生成背剑

头文件顶部新增前置声明：

```cpp
class UBossCombatComponent;
class ABossWeapon;
```

`public:` 区域新增：

```cpp
	// 武器生成类：在 BP_Boss 里填 BP_BossWeapon。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Weapon", meta=(AllowPrivateAccess="true"))
	TSubclassOf<ABossWeapon> BossWeaponClass;

	// 背部挂点 / 手部挂点（在 SK_DKF_Full 骨架上创建）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Weapon", meta=(AllowPrivateAccess="true"))
	FName BackSocketName = TEXT("BossWeapon_Back");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Weapon", meta=(AllowPrivateAccess="true"))
	FName HandSocketName = TEXT("BossWeapon_Hand");

	// 招式蒙太奇（在 BP_Boss 里配置）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Anim", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> DrawSwordMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Anim", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> NormalAttackMontage;

	// 三连击：单个完整三连蒙太奇（动画本身包含三段挥砍）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Anim", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> ThreeComboMontage;

	FORCEINLINE UBossCombatComponent* GetBossCombatComponent() const { return BossCombatComponent; }
	FORCEINLINE UAnimMontage* GetDrawSwordMontage() const { return DrawSwordMontage; }
	FORCEINLINE UAnimMontage* GetNormalAttackMontage() const { return NormalAttackMontage; }
	FORCEINLINE UAnimMontage* GetThreeComboMontage() const { return ThreeComboMontage; }
```

`private:` 区域新增：

```cpp
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UBossCombatComponent> BossCombatComponent;
```

### 7.2 源文件

> 【改动位置】文件 `BossCharacter.cpp`
> 【新增内容】构造函数创建战斗组件；`BeginPlay` 生成并背挂武器

新增 include：

```cpp
#include "Components/Combat/BossCombatComponent.h"
#include "Items/Weapons/BossWeapon.h"
#include "MyGameplayTags.h"
```

构造函数里（`EnemyUIComponent` 创建之后）新增：

```cpp
	BossCombatComponent = CreateDefaultSubobject<UBossCombatComponent>(TEXT("BossCombatComponent"));
```

`BeginPlay()` 里，家点与 Health 监听之后新增：

```cpp
	// 出生背剑：生成剑并挂到背部挂点，注册进战斗组件（先不当作装备）。
	if (BossWeaponClass && GetMesh() && BossCombatComponent)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.Instigator = this;

		if (ABossWeapon* Weapon = GetWorld()->SpawnActor<ABossWeapon>(
			BossWeaponClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams))
		{
			Weapon->AttachToComponent(
				GetMesh(),
				FAttachmentTransformRules::SnapToTargetNotIncludingScale,
				BackSocketName);

			BossCombatComponent->RegisterSpawnedWeapon(
				MyGameplayTags::Boss_Weapon_Sword,
				Weapon,
				false);
		}
	}
```

编译闸门 5。

---

## 8. 第六步：新建 UFirstBossGameplayAbility（能力基类）与 UFirstGE_BossCooldown

### 8.1 冷却 GE

用编辑器向导创建（父类 **`GameplayEffect`**，即 `UGameplayEffect`），类名 `FirstGE_BossCooldown`，文件放 `AbilitySystem/GameplayEffects/`，整体替换：

> 【改动位置】新建文件 `FirstGE_BossCooldown.h` / `FirstGE_BossCooldown.cpp`
> 【新增内容】HasDuration 冷却 GE，时长由 SetByCaller 传入

`FirstGE_BossCooldown.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "FirstGE_BossCooldown.generated.h"

/**
 * BOSS 招式冷却。时长由能力通过 SetByCaller 写入。
 */
UCLASS()
class FIRST_API UFirstGE_BossCooldown : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UFirstGE_BossCooldown();
};
```

`FirstGE_BossCooldown.cpp`：

```cpp
#include "AbilitySystem/GameplayEffects/FirstGE_BossCooldown.h"

#include "MyGameplayTags.h"

UFirstGE_BossCooldown::UFirstGE_BossCooldown()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;

	// 时长由能力里的 ApplyCooldown 用同一个标签写入。
	FSetByCallerFloat SetByCaller;
	SetByCaller.DataTag = MyGameplayTags::Boss_SetByCaller_CooldownDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
}
```

### 8.2 能力基类

用编辑器向导创建（父类 **`FirstGameplayAbility`**，即 `UFirstGameplayAbility`），类名 `FirstBossGameplayAbility`，文件放 `AbilitySystem/Abilities/Boss/`，整体替换：

> 【改动位置】新建文件 `FirstBossGameplayAbility.h` / `FirstBossGameplayAbility.cpp`
> 【新增内容】冷却时长 + `ApplyCooldown` 覆写；BOSS 角色/战斗组件访问器；BOSS 伤害 Spec 生成

`FirstBossGameplayAbility.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/FirstGameplayAbility.h"
#include "FirstBossGameplayAbility.generated.h"

class ABossCharacter;
class UBossCombatComponent;
class UGameplayEffect;

/**
 * BOSS 动作/攻击能力的公共基类。
 * 派生类在构造函数里设置 CooldownDuration，并把自己的身份标签加入 AbilityTags 与 AssetTags。
 */
UCLASS()
class FIRST_API UFirstBossGameplayAbility : public UFirstGameplayAbility
{
	GENERATED_BODY()

public:
	// 本次招式的冷却秒数（派生类构造函数里设置）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Cooldown")
	float CooldownDuration = 3.f;

	ABossCharacter* GetBossCharacterFromActorInfo();
	UBossCombatComponent* GetBossCombatComponentFromActorInfo();

	// 生成一次 BOSS 近战伤害 Spec（复用玩家伤害 GE 与 ExecCalc）。
	FGameplayEffectSpecHandle MakeBossDamageEffectSpecHandle(
		TSubclassOf<UGameplayEffect> EffectClass,
		float InBaseDamage);

protected:
	// 用 CooldownDuration 写入 SetByCaller 后应用冷却 GE。
	virtual void ApplyCooldown(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) const override;
};
```

`FirstBossGameplayAbility.cpp`：

```cpp
#include "AbilitySystem/Abilities/Boss/FirstBossGameplayAbility.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "Character/BossCharacter.h"
#include "Components/Combat/BossCombatComponent.h"
#include "MyGameplayTags.h"

ABossCharacter* UFirstBossGameplayAbility::GetBossCharacterFromActorInfo()
{
	return Cast<ABossCharacter>(GetAvatarActorFromActorInfo());
}

UBossCombatComponent* UFirstBossGameplayAbility::GetBossCombatComponentFromActorInfo()
{
	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	return Boss ? Boss->GetBossCombatComponent() : nullptr;
}

FGameplayEffectSpecHandle UFirstBossGameplayAbility::MakeBossDamageEffectSpecHandle(
	TSubclassOf<UGameplayEffect> EffectClass,
	float InBaseDamage)
{
	check(EffectClass);

	UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponentFromActorInfo();
	check(ASC);

	FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
	ContextHandle.SetAbility(this);
	ContextHandle.AddSourceObject(GetAvatarActorFromActorInfo());
	ContextHandle.AddInstigator(GetAvatarActorFromActorInfo(), GetAvatarActorFromActorInfo());

	FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(EffectClass, GetAbilityLevel(), ContextHandle);

	// 复用玩家伤害公式的 SetByCaller 标签；连击段数固定 1（BOSS 三连的加成以后单独定）。
	SpecHandle.Data->SetSetByCallerMagnitude(MyGameplayTags::DK_SetByCaller_BaseDamage, InBaseDamage);
	SpecHandle.Data->SetSetByCallerMagnitude(MyGameplayTags::DK_SetByCaller_ComboCount, 1);

	return SpecHandle;
}

void UFirstBossGameplayAbility::ApplyCooldown(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo) const
{
	UGameplayEffect* CooldownGE = GetCooldownGameplayEffect();
	if (!CooldownGE)
	{
		return;
	}

	FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(
		CooldownGE->GetClass(),
		GetAbilityLevel(Handle, ActorInfo));

	// 冷却标签挂到本次 Effect 上，冷却结束自动移除。
	// GetCooldownTags() 返回 const FGameplayTagContainer*（可能为空），
	// AppendTags 需要引用，所以先判空再解引用。
	if (const FGameplayTagContainer* CooldownTags = GetCooldownTags())
	{
		SpecHandle.Data->DynamicGrantedTags.AppendTags(*CooldownTags);
	}
	SpecHandle.Data->SetSetByCallerMagnitude(
		MyGameplayTags::Boss_SetByCaller_CooldownDuration,
		CooldownDuration);

	ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, SpecHandle);
}
```

编译闸门 6。

---

## 9. 第七步：新建 GA_Boss_DrawSword（拔剑）

用编辑器向导创建（父类 **`FirstBossGameplayAbility`**），类名 `GA_Boss_DrawSword`，文件放 `AbilitySystem/Abilities/Boss/`，整体替换：

> 【改动位置】新建文件 `GA_Boss_DrawSword.h` / `GA_Boss_DrawSword.cpp`
> 【新增内容】CombatStart 事件触发 → 播放拔剑蒙太奇 → 挂手 + 加 WeaponDrawn 标签

`GA_Boss_DrawSword.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/Boss/FirstBossGameplayAbility.h"
#include "GA_Boss_DrawSword.generated.h"

/**
 * 拔剑：Boss.Event.CombatStart 触发（首次索敌成功）。
 * 播放拔剑蒙太奇；挂手后添加 Boss.Status.WeaponDrawn（攻击能力的前置条件）。
 */
UCLASS()
class FIRST_API UGA_Boss_DrawSword : public UFirstBossGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Boss_DrawSword();

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	// 拔剑蒙太奇里的 AttachToHand Notify 触发时挂手。
	UFUNCTION()
	void HandleWeaponAttachEvent(FGameplayEventData Payload);

	UFUNCTION()
	void HandleMontageCompleted();

	UFUNCTION()
	void HandleMontageInterrupted();

	void AttachWeaponToHand();
	void FinishDraw(bool bWasCancelled);

	bool bWeaponAttached = false;
};
```

`GA_Boss_DrawSword.cpp`：

```cpp
#include "AbilitySystem/Abilities/Boss/GA_Boss_DrawSword.h"

#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "Character/BossCharacter.h"
#include "Components/Combat/BossCombatComponent.h"
#include "Items/Weapons/FirstWeaponBase.h"
#include "MyGameplayTags.h"

UGA_Boss_DrawSword::UGA_Boss_DrawSword()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	// 身份标签：5.6 中 AbilityTags 已弃用，统一用 SetAssetTags（构造函数里设置）。
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::Boss_Ability_DrawSword);
	SetAssetTags(AssetTags);

	// 拔过就不能再拔；死亡不拔。
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_WeaponDrawn);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);
	// 拔剑期间挂"拔剑中"标签：行为树据此挡住追击/巡逻，等拔完剑再动。
	ActivationOwnedTags.AddTag(MyGameplayTags::Boss_Status_DrawSword);

	// CombatStart 事件触发。【UE 5.6】旧名 FGameplayAbilityTrigger 已改名 FAbilityTriggerData。
	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = MyGameplayTags::Boss_Event_CombatStart;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(Trigger);
}

void UGA_Boss_DrawSword::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	if (!Boss)
	{
		FinishDraw(true);
		return;
	}

	bWeaponAttached = false;

	// 监听拔剑蒙太奇里的挂手 Notify（复用 DK.Event.Weapon.AttachToHand）。
	UAbilityTask_WaitGameplayEvent* AttachTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::DK_Event_Weapon_AttachToHand,
			nullptr,
			false,
			true);
	AttachTask->EventReceived.AddDynamic(this, &ThisClass::HandleWeaponAttachEvent);
	AttachTask->ReadyForActivation();

	UAnimMontage* Montage = Boss->GetDrawSwordMontage();
	if (!Montage)
	{
		// 没有拔剑动画时直接挂手，保证流程可跑通。
		AttachWeaponToHand();
		FinishDraw(false);
		return;
	}

	UAbilityTask_PlayMontageAndWait* MontageTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			TEXT("DrawSwordMontage"),
			Montage);
	MontageTask->OnCompleted.AddDynamic(this, &ThisClass::HandleMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::HandleMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &ThisClass::HandleMontageInterrupted);
	MontageTask->ReadyForActivation();
}

void UGA_Boss_DrawSword::HandleWeaponAttachEvent(FGameplayEventData Payload)
{
	AttachWeaponToHand();
}

void UGA_Boss_DrawSword::HandleMontageCompleted()
{
	AttachWeaponToHand();
	FinishDraw(false);
}

void UGA_Boss_DrawSword::HandleMontageInterrupted()
{
	AttachWeaponToHand();
	FinishDraw(true);
}

void UGA_Boss_DrawSword::AttachWeaponToHand()
{
	if (bWeaponAttached)
	{
		return;
	}
	bWeaponAttached = true;

	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	UBossCombatComponent* Combat = GetBossCombatComponentFromActorInfo();
	if (!Boss || !Combat || !Boss->GetMesh())
	{
		return;
	}

	if (AFirstWeaponBase* Weapon = Combat->GetCharacterCarriedWeaponByTag(MyGameplayTags::Boss_Weapon_Sword))
	{
		Weapon->AttachToComponent(
			Boss->GetMesh(),
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			Boss->HandSocketName);
	}

	// 标记当前装备武器：武器碰撞开关（ToggleWeaponCollision）依赖这个标签。
	Combat->CurrentEquippedWeaponTag = MyGameplayTags::Boss_Weapon_Sword;

	if (UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponentFromActorInfo())
	{
		ASC->AddLooseGameplayTag(MyGameplayTags::Boss_Status_WeaponDrawn);
	}
}

void UGA_Boss_DrawSword::FinishDraw(bool bWasCancelled)
{
	if (!IsActive())
	{
		return;
	}

	EndAbility(
		CurrentSpecHandle,
		CurrentActorInfo,
		CurrentActivationInfo,
		true,
		bWasCancelled);
}
```

编译闸门 7。

---

## 10. 第八步：新建 GA_Boss_NormalAttack（普通攻击）

用编辑器向导创建（父类 **`FirstBossGameplayAbility`**），类名 `GA_Boss_NormalAttack`，整体替换：

> 【改动位置】新建文件 `GA_Boss_NormalAttack.h` / `GA_Boss_NormalAttack.cpp`
> 【新增内容】普通攻击：要求已拔剑、带冷却、命中时结算伤害

`GA_Boss_NormalAttack.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/Boss/FirstBossGameplayAbility.h"
#include "GA_Boss_NormalAttack.generated.h"

/**
 * BOSS 普通攻击（单段）。
 */
UCLASS()
class FIRST_API UGA_Boss_NormalAttack : public UFirstBossGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Boss_NormalAttack();

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	UFUNCTION()
	void HandleMeleeHit(FGameplayEventData Payload);

	UFUNCTION()
	void HandleMontageCompleted();

	UFUNCTION()
	void HandleMontageInterrupted();

	// UE 5.6 基类已移除 CooldownTags，需要自己声明并覆写 GetCooldownTags。
	FGameplayTagContainer CooldownTags;
	virtual const FGameplayTagContainer* GetCooldownTags() const override;
};
```

`GA_Boss_NormalAttack.cpp`：

```cpp
#include "AbilitySystem/Abilities/Boss/GA_Boss_NormalAttack.h"

#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystem/GameplayEffects/FirstGE_Damage.h"
#include "Character/BossCharacter.h"
#include "MyGameplayTags.h"

UGA_Boss_NormalAttack::UGA_Boss_NormalAttack()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	// 身份 + 类别标签：5.6 中 AbilityTags 已弃用，统一用 SetAssetTags。
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::Boss_Ability_Attack);
	AssetTags.AddTag(MyGameplayTags::Boss_Ability_Attack_Normal);
	SetAssetTags(AssetTags);

	// 没拔剑不放招；受击/死亡不放招。
	ActivationRequiredTags.AddTag(MyGameplayTags::Boss_Status_WeaponDrawn);
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Staggered);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);
	// 攻击期间挂"攻击中"标签：行为树据此挡住追击/巡逻，避免边攻击边位移。
	ActivationOwnedTags.AddTag(MyGameplayTags::Boss_Status_Attacking);

	// 冷却。
	CooldownGameplayEffectClass = UFirstGE_BossCooldown::StaticClass();
	CooldownTags.AddTag(MyGameplayTags::Boss_Cooldown_Attack_Normal);
	CooldownDuration = 2.5f;
}

const FGameplayTagContainer* UGA_Boss_NormalAttack::GetCooldownTags() const
{
	return &CooldownTags;
}

void UGA_Boss_NormalAttack::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	if (!Boss)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 监听本次挥砍的命中事件，命中后结算伤害。
	UAbilityTask_WaitGameplayEvent* HitTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::DK_Event_MeleeHit,
			nullptr,
			false,
			true);
	HitTask->EventReceived.AddDynamic(this, &ThisClass::HandleMeleeHit);
	HitTask->ReadyForActivation();

	UAnimMontage* Montage = Boss->GetNormalAttackMontage();
	if (!Montage)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UAbilityTask_PlayMontageAndWait* MontageTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			TEXT("NormalAttackMontage"),
			Montage);
	MontageTask->OnCompleted.AddDynamic(this, &ThisClass::HandleMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::HandleMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &ThisClass::HandleMontageInterrupted);
	MontageTask->ReadyForActivation();
}

void UGA_Boss_NormalAttack::HandleMeleeHit(FGameplayEventData Payload)
{
	if (!Payload.Target)
	{
		return;
	}

	// 普通攻击基础伤害 20；以后可以迁到 BOSS 数据资产。
	FGameplayEffectSpecHandle SpecHandle =
		MakeBossDamageEffectSpecHandle(UFirstGE_Damage::StaticClass(), 20.f);

	// FGameplayEventData.Target 是 const 指针，ApplyEffectSpecHandleToTarget 需要 AActor*。
	ApplyEffectSpecHandleToTarget(const_cast<AActor*>(Payload.Target.Get()), SpecHandle);
}

void UGA_Boss_NormalAttack::HandleMontageCompleted()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UGA_Boss_NormalAttack::HandleMontageInterrupted()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}
```

编译闸门 8。

---

## 11. 第九步：新建 GA_Boss_ThreeCombo（三连击，一个技能）

用编辑器向导创建（父类 **`FirstBossGameplayAbility`**），类名 `GA_Boss_ThreeCombo`，整体替换：

> 【改动位置】新建文件 `GA_Boss_ThreeCombo.h` / `GA_Boss_ThreeCombo.cpp`
> 【新增内容】一个技能：**单个完整三连蒙太奇**（动画本身包含三段挥砍），
> 蒙太奇里放 **3 个武器碰撞窗口**，每命中一次结算一次伤害。
> 如果以后想改成"三段独立蒙太奇"，再加回 `StartComboStep` 分段逻辑即可。

`GA_Boss_ThreeCombo.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/Boss/FirstBossGameplayAbility.h"
#include "GA_Boss_ThreeCombo.generated.h"

/**
 * BOSS 三连击（一个技能）。
 * 播放单个完整三连蒙太奇；蒙太奇里的 3 个碰撞窗口各触发一次命中结算。
 */
UCLASS()
class FIRST_API UGA_Boss_ThreeCombo : public UFirstBossGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Boss_ThreeCombo();

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	UFUNCTION()
	void HandleMeleeHit(FGameplayEventData Payload);

	UFUNCTION()
	void HandleMontageCompleted();

	UFUNCTION()
	void HandleMontageInterrupted();

	// UE 5.6 基类已移除 CooldownTags，需要自己声明并覆写 GetCooldownTags。
	FGameplayTagContainer CooldownTags;
	virtual const FGameplayTagContainer* GetCooldownTags() const override;
};
```

`GA_Boss_ThreeCombo.cpp`：

```cpp
#include "AbilitySystem/Abilities/Boss/GA_Boss_ThreeCombo.h"

#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystem/GameplayEffects/FirstGE_Damage.h"
#include "Character/BossCharacter.h"
#include "MyGameplayTags.h"

UGA_Boss_ThreeCombo::UGA_Boss_ThreeCombo()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	// 身份 + 类别标签：5.6 中 AbilityTags 已弃用，统一用 SetAssetTags。
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::Boss_Ability_Attack);
	AssetTags.AddTag(MyGameplayTags::Boss_Ability_Attack_ThreeCombo);
	SetAssetTags(AssetTags);

	ActivationRequiredTags.AddTag(MyGameplayTags::Boss_Status_WeaponDrawn);
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Staggered);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);
	// 攻击期间挂"攻击中"标签：行为树据此挡住追击/巡逻，避免边攻击边位移。
	ActivationOwnedTags.AddTag(MyGameplayTags::Boss_Status_Attacking);

	CooldownGameplayEffectClass = UFirstGE_BossCooldown::StaticClass();
	CooldownTags.AddTag(MyGameplayTags::Boss_Cooldown_Attack_ThreeCombo);
	CooldownDuration = 6.f;
}

const FGameplayTagContainer* UGA_Boss_ThreeCombo::GetCooldownTags() const
{
	return &CooldownTags;
}

void UGA_Boss_ThreeCombo::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// 整个三连期间都监听命中。
	UAbilityTask_WaitGameplayEvent* HitTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::DK_Event_MeleeHit,
			nullptr,
			false,
			true);
	HitTask->EventReceived.AddDynamic(this, &ThisClass::HandleMeleeHit);
	HitTask->ReadyForActivation();

	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	UAnimMontage* Montage = Boss ? Boss->GetThreeComboMontage() : nullptr;
	if (!Montage)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	UAbilityTask_PlayMontageAndWait* MontageTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			TEXT("ThreeComboMontage"),
			Montage);
	MontageTask->OnCompleted.AddDynamic(this, &ThisClass::HandleMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::HandleMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &ThisClass::HandleMontageInterrupted);
	MontageTask->ReadyForActivation();
}

void UGA_Boss_ThreeCombo::HandleMeleeHit(FGameplayEventData Payload)
{
	if (!Payload.Target)
	{
		return;
	}

	// 每段命中伤害 22（先写死，以后迁数据资产）。
	FGameplayEffectSpecHandle SpecHandle =
		MakeBossDamageEffectSpecHandle(UFirstGE_Damage::StaticClass(), 22.f);

	// FGameplayEventData.Target 是 const 指针，ApplyEffectSpecHandleToTarget 需要 AActor*。
	ApplyEffectSpecHandleToTarget(const_cast<AActor*>(Payload.Target.Get()), SpecHandle);
}

void UGA_Boss_ThreeCombo::HandleMontageCompleted()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UGA_Boss_ThreeCombo::HandleMontageInterrupted()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}
```

编译闸门 9。

---

## 12. 第十步：新建选招与放招任务

### 12.1 UBTTask_BossSelectAttack（选招）

用编辑器向导创建（父类 **`BTTaskNode`**），类名 `BTTask_BossSelectAttack`，文件放 `AI/Tasks/`，整体替换：

> 【改动位置】新建文件 `BTTask_BossSelectAttack.h` / `BTTask_BossSelectAttack.cpp`
> 【新增内容】招式选项表 + 按"冷却标签 + 距离"过滤后随机选一个，写入黑板 `AttackTag`

`BTTask_BossSelectAttack.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_BossSelectAttack.generated.h"

USTRUCT(BlueprintType)
struct FBossAttackOption
{
	GENERATED_BODY()

	// 招式的身份标签（AbilityTags 里的那个）。
	UPROPERTY(EditAnywhere, Category="Boss|Attack")
	FGameplayTag AbilityTag;

	// 该招式的冷却标签（冷却中不会选中）。
	UPROPERTY(EditAnywhere, Category="Boss|Attack")
	FGameplayTag CooldownTag;

	// 最大使用距离（超过该距离不选）。
	UPROPERTY(EditAnywhere, Category="Boss|Attack", meta=(ClampMin="0.0"))
	float MaxRange = 300.f;
};

/**
 * 选招：过滤掉冷却中/超距离的招式，从可用招式里随机选一个，
 * 把选中的身份标签写入黑板键 AttackTag。
 */
UCLASS()
class FIRST_API UBTTask_BossSelectAttack : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_BossSelectAttack();

	// 在行为树节点详情里配置招式表。
	UPROPERTY(EditAnywhere, Category="Boss|Attack")
	TArray<FBossAttackOption> AttackOptions;

	UPROPERTY(EditAnywhere, Category="Boss|Attack")
	FName AttackTagKey = TEXT("AttackTag");

	virtual EBTNodeResult::Type ExecuteTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;
};
```

`BTTask_BossSelectAttack.cpp`：

```cpp
#include "AI/Tasks/BTTask_BossSelectAttack.h"

#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/BossCharacter.h"

UBTTask_BossSelectAttack::UBTTask_BossSelectAttack()
{
	NodeName = TEXT("Boss Select Attack");
}

EBTNodeResult::Type UBTTask_BossSelectAttack::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	ABossCharacter* Boss = AIController
		? Cast<ABossCharacter>(AIController->GetPawn())
		: nullptr;
	AActor* Target = BB
		? Cast<AActor>(BB->GetValueAsObject(TEXT("TargetActor")))
		: nullptr;

	if (!BB || !Boss || !Target || !Boss->GetAbilitySystemComponent())
	{
		return EBTNodeResult::Failed;
	}

	const float Distance = FVector::Dist(
		Boss->GetActorLocation(),
		Target->GetActorLocation());

	UAbilitySystemComponent* ASC = Boss->GetAbilitySystemComponent();

	// 收集可用招式：冷却标签不在身上 + 距离在范围内。
	TArray<int32> EligibleIndexes;
	for (int32 i = 0; i < AttackOptions.Num(); ++i)
	{
		const FBossAttackOption& Option = AttackOptions[i];
		if (!Option.AbilityTag.IsValid())
		{
			continue;
		}
		if (Option.CooldownTag.IsValid() && ASC->HasMatchingGameplayTag(Option.CooldownTag))
		{
			continue;
		}
		if (Distance > Option.MaxRange)
		{
			continue;
		}
		EligibleIndexes.Add(i);
	}

	if (EligibleIndexes.Num() == 0)
	{
		return EBTNodeResult::Failed;
	}

	const int32 SelectedIndex = EligibleIndexes[FMath::RandRange(0, EligibleIndexes.Num() - 1)];
	BB->SetValueAsName(AttackTagKey, AttackOptions[SelectedIndex].AbilityTag.GetTagName());

	return EBTNodeResult::Succeeded;
}
```

### 12.2 UBTTask_BossActivateAbilityByTag（放招）

用编辑器向导创建（父类 **`BTTaskNode`**），类名 `BTTask_BossActivateAbilityByTag`，整体替换：

> 【改动位置】新建文件 `BTTask_BossActivateAbilityByTag.h` / `BTTask_BossActivateAbilityByTag.cpp`
> 【新增内容】读取黑板 AttackTag，按标签激活 BOSS 能力

`BTTask_BossActivateAbilityByTag.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_BossActivateAbilityByTag.generated.h"

/**
 * 读取黑板 AttackTag，调用 TryActivateAbilitiesByTag 激活对应能力。
 */
UCLASS()
class FIRST_API UBTTask_BossActivateAbilityByTag : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_BossActivateAbilityByTag();

	UPROPERTY(EditAnywhere, Category="Boss|Attack")
	FName AttackTagKey = TEXT("AttackTag");

	virtual EBTNodeResult::Type ExecuteTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;
};
```

`BTTask_BossActivateAbilityByTag.cpp`：

```cpp
#include "AI/Tasks/BTTask_BossActivateAbilityByTag.h"

#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/BossCharacter.h"

UBTTask_BossActivateAbilityByTag::UBTTask_BossActivateAbilityByTag()
{
	NodeName = TEXT("Boss Activate Ability By Tag");
}

EBTNodeResult::Type UBTTask_BossActivateAbilityByTag::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	ABossCharacter* Boss = AIController
		? Cast<ABossCharacter>(AIController->GetPawn())
		: nullptr;

	if (!BB || !Boss || !Boss->GetAbilitySystemComponent())
	{
		return EBTNodeResult::Failed;
	}

	const FName TagName = BB->GetValueAsName(AttackTagKey);
	const FGameplayTag AbilityTag = FGameplayTag::RequestGameplayTag(TagName, false);
	if (!AbilityTag.IsValid())
	{
		return EBTNodeResult::Failed;
	}

	FGameplayTagContainer Tags;
	Tags.AddTag(AbilityTag);

	const bool bActivated = Boss->GetAbilitySystemComponent()->TryActivateAbilitiesByTag(Tags, true);
	return bActivated ? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
}
```

编译闸门 10。

---

## 13. 第十一步：对峙结束后发 CombatStart 事件（拔剑时机）

> 【改动位置】`BTTask_BossAlert.cpp`（对峙任务结束处）+ `BossAIController.cpp`（删掉旧触发）
> 【新增内容】`CombatStart` 不再在"看见玩家"时发，而是在**对峙计时结束**时发：
> 玩家进入警戒 → BOSS 对峙（StareDuration 秒）→ 对峙结束 → 拔剑 + 追击。

### 13.1 BTTask_BossAlert：对峙结束时发事件

`BTTask_BossAlert.cpp` 新增 include：

```cpp
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Character/BossCharacter.h"
#include "MyGameplayTags.h"
```

`TickTask` 里"对峙结束"那一段改为：

```cpp
	if (Memory->ElapsedTime >= StareDuration)
	{
		// 对峙结束：允许追击，并触发拔剑（CombatStart）。
		BB->SetValueAsBool(TEXT("bShouldChase"), true);

		if (ABossCharacter* Boss = Cast<ABossCharacter>(AIController->GetPawn()))
		{
			FGameplayEventData EventData;
			EventData.Instigator = Target;
			EventData.Target = Target;
			UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
				Boss,
				MyGameplayTags::Boss_Event_CombatStart,
				EventData);
		}

		// 【UE 5.6 修正】两参数版本，不要带 NodeMemory。
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}
```

### 13.2 BossAIController：删掉旧的"看见就触发"

`BossAIController.cpp` 的 `HandleTargetPerceptionUpdated` 里，删掉 `bFirstSight` 和整个 `CombatStart` 发送块，恢复成简单的写入：

```cpp
	if (Stimulus.WasSuccessfullySensed())
	{
		// 发现玩家：写入目标，并让 AIController 的 Focus 对准它。
		BB->SetValueAsObject(TEXT("TargetActor"), Candidate);
		BB->SetValueAsBool(TEXT("bPlayerDetected"), true);
		SetFocus(Candidate, EAIFocusPriority::Gameplay);
	}
```

> 拔剑时机由行为树里的 Boss Alert 节点 **Stare Duration** 控制（默认 3 秒；要对峙 2 秒就改成 2.0）。

编译闸门 11。

---

## 14. 第十二步：编辑器资产配置

### 14.1 创建挂点（Socket）

1. ⚠️ 骨架注意：BP_Boss 当前用**男骑士（SKM_DKM_Full）**，挂点必须建在 **`SK_DKM_Full`** 骨架上，不是 DKF 女骑士。
2. 在**骨架树（Skeleton Tree）**里选中手部骨骼（如 `hand_r`），右键 → **添加插槽（Add Socket）**，命名 `BossWeapon_Hand`；
3. 选中背部骨骼（如 `spine_03` 或 `pelvis`），右键 → 添加插槽，命名 `BossWeapon_Back`；
4. 选中插槽，调整位置/旋转，让剑在手里、背上的姿势正确（可把 `SM_DKM_Sword` 拖进插槽预览）。

### 14.2 创建蒙太奇

| 蒙太奇 | 来源动画 | 说明 |
|---|---|---|
| `AM_Boss_Attack_Normal` | `Anim_DKM_Attack_01` | 普通攻击 |
| `AM_Boss_Attack_Combo` | 单个完整三连动画（含三段挥砍） | 三连击（一个蒙太奇） |
| `AM_Boss_DrawSword` | 见下方说明 | 拔剑 |

每个**攻击蒙太奇**里添加通知状态（Notify State）**Weapon Collision**（`FirstANS_WeaponCollision`），覆盖有效挥砍帧（参照主角蒙太奇的配置）。
⚠️ `AM_Boss_Attack_Combo` 因为是单个完整三连动画，要放 **3 个 Weapon Collision 窗口**（对应三段挥砍），每段各结算一次伤害。

拔剑动画说明：DK 包没有现成拔剑动画。两种做法：

- **占位（推荐先跑通）**：用 `Anim_DKM_Idle_Alert` 建一个约 0.5 秒的短蒙太奇，挂手 Notify 放在开头；
- **正式 A：Mixamo 下载**：搜 `draw sword / unsheathe / equip` 类动画，下载时选 **Without Skin + FBX for Unreal**，导入后用 **IK Rig + IK Retargeter** 重定向到 `SK_DKM_Full`（流程照搬《八向闪避与闪避接奔跑实现指导》第 4 节），再做蒙太奇。注意 Mixamo 动画是空手的，剑靠挂手 Notify 切到手上；
- **正式 B**：把 SwordAnimsetPro 的装备动画 IK 重定向到 `SK_DKM_Full` 后做蒙太奇。

`AM_Boss_DrawSword` 里添加一个**通知（Notify）**：`DK Gameplay Event`（`AnimNotify_DKGameplayEvent`），`EventTag` 选 `DK.Event.Weapon.AttachToHand`，放在"手握住剑"的那一帧。

#### 14.2.1 补充：Mixamo 拔剑动画完整接入指南（2026-08）

> 本节目标：把 Mixamo 下载的拔剑动画，经过 IK 重定向接入 `SK_DKM_Full`，做成
> `AM_Boss_DrawSword`，并让 `GA_Boss_DrawSword` 在"手握住剑"的那一帧把剑切到手上。
> 核心思路与《八向闪避与闪避接奔跑实现指导》第 4 节一致：**源骨架 IK Rig → IK 重定向器 → 目标骨架**。

##### 1. 前置条件

- First 能正常编译运行，BOSS 使用男骑士网格 `SKM_DKM_Full`（骨架 `SK_DKM_Full`）；
- 一个可登录 [mixamo.com](https://www.mixamo.com) 的 Adobe 账号（免费）；
- 建议先通读《八向闪避与闪避接奔跑实现指导》第 4 节（IK 绑定 / IK 重定向器），术语一致。

##### 2. 第一步：Mixamo 下载动画

1. 打开 Mixamo 网站并登录，左侧搜索栏试关键词：`draw sword`、`unsheathe`、`sword out`、`equip`；
2. 选中合适的动画，右侧**下载（Download）**面板按下面配置：

| 选项 | 值 | 说明 |
|---|---|---|
| Character（角色） | 任意（推荐 Mixamo 自带角色） | 动画是通用的，最后会重定向 |
| Skin（皮肤） | **Without Skin** | 只要动画，不要模型 |
| Format（格式） | **FBX for Unreal** | 千万别选 Unity 版 |
| Pose（姿势） | **T-Pose** | 绑定姿势 |
| Keyframe Reduction | 默认 | 精简关键帧，不用改 |

3. 下载保存（例如 `Mixamo_DrawSword.fbx`）。

> ⚠️ Mixamo 的动画库以空手身体动作为主，**剑类动作很少**。如果搜不到合适的拔剑动作，
> 就退回 14.2 的"占位"方案（`Anim_DKM_Idle_Alert` 短蒙太奇 + 挂手 Notify 放开头），流程先跑通。

##### 3. 第二步：导入 First 项目

1. 内容浏览器新建 `/Game/Enemy/AnimBP/Mixamo/`（或你自己喜欢的目录）；
2. 右键 → **导入（Import）** → 选择 `Mixamo_DrawSword.fbx`；
3. **FBX 导入选项（FBX Import Options）**关键设置：

| 选项 | 值 |
|---|---|
| 网格体（Mesh） | **不导入**（如果默认导入，导入后把多余网格删掉） |
| 骨架（Skeleton） | **创建新骨架**（生成 Mixamo 自己的骨架） |
| 导入动画（Import Animation） | **勾选** |

4. 导入后应得到：一个 Mixamo 骨架 + 一个动画序列；
5. 双击动画序列预览，确认动作正确、节奏合适。

##### 4. 第三步：创建 IK 绑定（IK Rig）

1. 内容浏览器选中 **Mixamo 骨架** → 右键 → **创建 IK 绑定（Create IK Rig）**，命名如 `IK_Mixamo`；
2. 选中 **`SK_DKM_Full`** → 右键 → **创建 IK 绑定**，命名如 `IK_DKM`（如果之前没有）；
3. IK Rig 的作用：告诉重定向器"这个骨架的根、髋、脊柱、手、脚分别是哪些骨骼"。

##### 5. 第四步：创建 IK 重定向器（IK Retargeter）

1. 内容浏览器右键 → **动画（Animation）→ IK 重定向器（IK Retargeter）**，命名 `RT_Mixamo_To_DKM`；
2. 打开后设置：
   - **源骨架（Source Mesh / Source IK Rig）** = `IK_Mixamo`（Mixamo）；
   - **目标骨架（Target Mesh / Target IK Rig）** = `IK_DKM`（SK_DKM_Full）；
3. 点 **自动映射（Auto Map）**；
4. **手动核对以下骨骼链**（自动映射通常够用，但手部/手指容易错）：

| 骨骼链 | 必须映射正确的骨骼 |
|---|---|
| 根与髋 | Root、Hips（Pelvis） |
| 脊柱 | Spine、Spine1、Spine2、Neck、Head |
| 手臂 | Clavicle、UpperArm、LowerArm、Hand（左右都要） |
| 手指 | 每根手指的 3 节（Thumb/Index/Middle/Ring/Pinky） |
| 腿 | Thigh、Calf、Foot、Toe |

5. 在重定向器预览里播放源动画，观察**目标角色**动作是否正确：
   - 整体姿势不 T 型、不扭曲；
   - 手型/朝向自然；
   - 身体位移合理（Mixamo 动画一般是原地动作，不应乱飞）。

##### 6. 第五步：重定向动画

1. 在重定向器面板里把源动画拖进 **Asset 列表（Add to Anim Assets）**；
2. 或直接右键源动画 → **在目标骨架下创建重定向副本（Retarget Animation Assets）**，目标选 `SK_DKM_Full`；
3. 得到重定向后的动画（例如 `Anim_DKM_DrawSword`），双击预览确认无错位；
4. 如果动画带位移/脚滑，见第 8 步处理根运动。

##### 7. 第六步：创建蒙太奇 AM_Boss_DrawSword

1. 右键重定向后的动画 → **创建蒙太奇（Create Montage）**，命名 `AM_Boss_DrawSword`，保存到 `/Game/Enemy/AnimBP/Montages/`；
2. 检查蒙太奇：**骨架 = `SK_DKM_Full`**、**插槽（Slot）= `DefaultSlot`**；
3. 在"手握住剑"的那一帧添加**通知（Notify）**：`DK Gameplay Event`（`AnimNotify_DKGameplayEvent`），**EventTag 选 `DK.Event.Weapon.AttachToHand`**；
   - 这一帧之前，剑保持背部挂点（`BossWeapon_Back`）；
   - 这一帧触发后，`GA_Boss_DrawSword` 的 `HandleWeaponAttachEvent` 会把剑切到 `BossWeapon_Hand` 并加 `Boss.Status.WeaponDrawn` 标签；
4. 编译并保存蒙太奇。

##### 8. 第七步：根运动与滑步检查

- 播放时**脚在地面滑动/身体漂移** → 源动画带根位移：
  1. 复制重定向后的动画到项目目录（不要改原件）；
  2. 打开副本 → 资产详情 → **根运动（Root Motion）** → 勾选**强制根锁定（Force Root Lock）**，或关闭"启用根运动（Enable Root Motion）"；
  3. 用副本重建蒙太奇（或把蒙太奇里的动画换成副本）。
- Mixamo 动画大多是原地动作，一般不需要这一步；遇到再处理。

##### 9. 第八步：配置 BP_Boss

打开 `BP_Boss` → **类默认值**：

| 属性 | 值 |
|---|---|
| Boss \| Anim → Draw Sword Montage | `AM_Boss_DrawSword` |

编译并保存。

##### 10. 验收清单（PIE）

| 步骤 | 操作 | 预期 |
|---|---|---|
| 拔剑触发 | 玩家进入警戒 → 首次索敌 | BOSS 播放 `AM_Boss_DrawSword` |
| 挂手 | 动画播放到挂手帧 | 剑从背部切到手上 |
| 状态 | 拔剑结束后 | BOSS 拥有 `Boss.Status.WeaponDrawn`（`ShowDebug AbilitySystem` 可看） |
| 攻击解锁 | 拔剑后 | 攻击分支能选招放招 |
| 交还动画 | 拔剑蒙太奇播完 | 回到 Locomotion（Idle/Walk），不卡在拔剑姿势 |

##### 11. 常见问题与排查

| 现象 | 原因 | 修复 |
|---|---|---|
| 目标角色 T 型 / 骨骼错位 | 重定向骨骼映射不对 | 回到第 5 步逐链核对：根、髋、脊柱、手臂、手、手指；重新自动映射后手动修正 |
| 手没握住剑 / 手势怪异 | 手腕或手指链映射错，或动画本身手型不匹配 | 修正 Hand/手指映射；或在重定向器里给手部骨骼加旋转偏移 |
| 动画不播放 | 蒙太奇 Slot 名不是 `DefaultSlot`，或骨架不是 `SK_DKM_Full` | 检查蒙太奇属性 |
| 剑没有切到手上 | 挂手 Notify 没放，或 EventTag 不一致 | 确认 Notify 用的 `AnimNotify_DKGameplayEvent` + `DK.Event.Weapon.AttachToHand`，且 `GA_Boss_DrawSword` 已授予 |
| 脚滑/漂移 | 动画带根位移 | 按第 8 步复制 + 强制根锁定 |
| 拔剑时剑不在背上 | 出生背剑失败或挂点错 | 检查 BP_Boss 的 Boss Weapon Class / Back Socket Name；确认 `BossCharacter::BeginPlay` 生成了剑并挂到背部 |

### 14.3 创建 BP_BossWeapon

1. 右键 → **蓝图类** → 搜索父类 `BossWeapon`（`ABossWeapon`），命名 `BP_BossWeapon`；
2. 类默认值里：`WeaponMesh` 选 `SM_DKM_Sword`（`/Game/Dark_Knight/Dark_Knight_Male/Meshes/`）；
3. 调整 `WeaponCollisionBox` 大小与位置，包住剑身；
4. 编译并保存。

### 14.4 配置 BP_Boss

打开 `BP_Boss` → 类默认值：

| 属性 | 值 |
|---|---|
| Boss \| Weapon → Boss Weapon Class | `BP_BossWeapon` |
| Boss \| Weapon → Back Socket Name | `BossWeapon_Back` |
| Boss \| Weapon → Hand Socket Name | `BossWeapon_Hand` |
| Boss \| Anim → Draw Sword Montage | `AM_Boss_DrawSword` |
| Boss \| Anim → Normal Attack Montage | `AM_Boss_Attack_Normal` |
| Boss \| Anim → Three Combo Montage | `AM_Boss_Attack_Combo` |
| Boss \| Combat → Normal Attack Damage | `20`（蓝图可调，普通攻击基础伤害） |
| Boss \| Combat → Three Combo Damage Per Hit | `22`（蓝图可调，三连每段基础伤害） |

编译并保存。

### 14.5 配置 DA_BossStartUpData

打开 `/Game/Enemy/Data/DA_BossStartUpData` → **ReactiveAbilities**，添加三个能力：

```text
GA_Boss_DrawSword
GA_Boss_NormalAttack
GA_Boss_ThreeCombo
```

保存。

### 14.6 配置 BB_Boss 与 BT_Boss

1. `BB_Boss` 新增黑板键 **`AttackTag`（Name）** 和 **`bIsBusy`（Bool）**；
2. `BT_Boss` 的**攻击分支**替换成下面的结构（装饰器仍是 `bInAttackRange == true`，Abort = Lower Priority）：

```text
攻击分支（序列 Sequence）
  1. Boss Select Attack
        Attack Options：
          - AbilityTag = Boss.Ability.Attack.Normal，CooldownTag = Boss.Cooldown.Attack.Normal，MaxRange = 300
          - AbilityTag = Boss.Ability.Attack.ThreeCombo，CooldownTag = Boss.Cooldown.Attack.ThreeCombo，MaxRange = 350
  2. Boss Activate Ability By Tag（AttackTag）
  3. 等待（Wait）≈ 1.5～2 秒（攻击后的对峙间隔）
```

3. 如果攻击分支还是旧的占位（Boss Alert），先删掉再按上面搭。

### 14.6.1 忙碌状态挡住追击/巡逻（防"边攻击边位移"和"拔剑滑步"）

**设计原则：行为树不关心"具体在做什么状态"，只关心"现在能不能追/能不能动"。**
攻击（`Boss.Status.Attacking`）和拔剑（`Boss.Status.DrawSword`）在激活时各自挂上自己的标签
（GAS 能力层语义），但决策层**只聚合出一个黑板键 `bIsBusy`**：

> 【改动位置】`BTService_UpdateBossState.h/.cpp`
> 【新增内容】`ActionBlockingTags` 标签容器（`EditAnywhere`，可在 BT 节点详情里配置）+ 写 `bIsBusy`

```cpp
// 头文件
UPROPERTY(EditAnywhere, Category="Boss|State")
FGameplayTagContainer ActionBlockingTags;

// cpp 构造函数（默认值，可在 BT 节点详情里增删）
ActionBlockingTags.AddTag(MyGameplayTags::Boss_Status_Attacking);
ActionBlockingTags.AddTag(MyGameplayTags::Boss_Status_DrawSword);

// cpp TickNode：只要任一"忙碌"标签存在 → bIsBusy = true
const bool bIsBusy = Boss->GetFirstAbilitySystemComponent()
	->HasAnyMatchingGameplayTags(ActionBlockingTags);
BB->SetValueAsBool(TEXT("bIsBusy"), bIsBusy);
```

然后在 `BT_Boss` 里：

- **追击分支**：装饰器 `bIsBusy == false`（中止 Abort = 两者 Both）；
- **巡逻分支**：装饰器 `bIsBusy == false`；
- 攻击分支不用改（攻击进行中本来就在执行它）。

效果：攻击/拔剑开始 → `bIsBusy = true` → 追击/巡逻不会激活、也不会打断当前动作；
动作完整播完（标签移除）→ 行为树重新按距离评估。

**以后新增状态（僵直、处决……）**：只需在 `Update Boss State` 服务节点的
`ActionBlockingTags` 里加一个标签，行为树不用加任何键和装饰器。

---

## 15. 编译顺序汇总

```text
闸门 1：MyGameplayTags
闸门 2：FirstANS_WeaponCollision 通用化
闸门 3：UBossCombatComponent
闸门 4：ABossWeapon
闸门 5：ABossCharacter（战斗组件/武器/挂点/蒙太奇/出生背剑）
闸门 6：UFirstGE_BossCooldown + UFirstBossGameplayAbility
闸门 7：GA_Boss_DrawSword
闸门 8：GA_Boss_NormalAttack
闸门 9：GA_Boss_ThreeCombo
闸门 10：BTTask_BossSelectAttack + BTTask_BossActivateAbilityByTag
闸门 11：BTTask_BossAlert（对峙结束发 CombatStart）+ BossAIController（还原为只写目标）
最后：编辑器配置（挂点/蒙太奇/BP_BossWeapon/BP_Boss/DA/BB/BT）
```

每完成一个闸门做一次普通 Build。

---

## 16. 测试清单

| 测试 | 操作 | 预期结果 |
|---|---|---|
| 出生背剑 | 运行 PIE 观察 BOSS | 剑挂在背上，不在手上 |
| 拔剑 | 玩家走进视野 | BOSS 播放拔剑动画，剑挂到手上 |
| 普通攻击 | 玩家在攻击范围内 | BOSS 随机放出单段挥砍，命中玩家扣血 |
| 攻击中离开范围 | BOSS 出手后玩家跑出攻击范围 | BOSS 先把攻击动画播完，再转追击；不会一边攻击一边位移 |
| 三连击 | 等待冷却后继续接近 | 三段动画顺序播放，每段有效帧命中都扣血 |
| 冷却 | 放完一招后观察 | 冷却期间同一招不会被连续选中 |
| 受击打断 | BOSS 出手时打它 | 招式被打断，播放受击动画 |
| 死亡 | 打死 BOSS | 死亡动画正常，不再放招 |
| 重复拔剑 | 退出再进视野 | 不会反复拔剑（WeaponDrawn 阻塞） |

---

## 17. 常见问题与排查

| 现象 | 原因 | 修复 |
|---|---|---|
| 剑没生成 | BP_Boss 的 Boss Weapon Class 没填，或挂点名错 | 检查 14.3 / 14.4 |
| 不拔剑 | CombatStart 没触发，或能力没授予，或标签不一致 | 检查 13（首次索敌）、14.5 ReactiveAbilities、`Boss.Event.CombatStart` |
| 拔剑后武器不在手上 | 挂手 Notify 没放，或挂点名错 | 检查 `AM_Boss_DrawSword` 里的 `DK.Event.Weapon.AttachToHand` Notify 与 14.4 挂点名 |
| 攻击不放 | 没拔剑（WeaponDrawn）/受击中/死亡/冷却中 | 用 `ShowDebug AbilitySystem` 看标签 |
| 攻击不命中 | 攻击蒙太奇没加 Weapon Collision 通知，或碰撞盒没包住剑 | 检查 14.2 通知状态与 14.3 碰撞盒 |
| 命中不掉血 | 战斗组件没接（BossCombatComponent），或伤害 Spec 没应用 | 检查 5 与 10/11 的 `HandleMeleeHit` |
| BOSS 一边攻击一边位移 | 追击分支在攻击中被激活（攻击能力独立于行为树运行） | 按 14.6.1：攻击能力挂 `Boss.Status.Attacking`，服务聚合到 `bIsBusy`，追击/巡逻装饰器挡 `bIsBusy == false` |
| 一看见玩家就拔剑 | CombatStart 在感知回调里发 | 按 13 节：把事件挪到 `BTTask_BossAlert` 对峙结束处发，感知回调只写目标 |
| 拔剑动画期间滑步 | 追击在拔剑完成前接管（行为树不知道在拔剑） | 拔剑能力挂 `Boss.Status.DrawSword`，服务聚合到 `bIsBusy`，追击/巡逻装饰器挡 `bIsBusy == false` |
| 玩家被打中后无法闪避/攻击/卸武器 | 玩家血量归零，`Shared.Status.Dead` 标签挡掉所有能力（且玩家暂无死亡反馈） | 先 `ShowDebug AbilitySystem` 看玩家 Health/Dead 标签确认；修数值：GE_Boss_Initialize 的 AttackPower 15→5（单次伤害约 60→20），或提高玩家防御；后续再做玩家死亡表现 |
| 三连只命中一段 | 完整三连蒙太奇里只放了一个 Weapon Collision 窗口 | 检查 `AM_Boss_Attack_Combo` 里要有 **3 个** Weapon Collision 窗口（分别覆盖三段挥砍） |
| 选招任务总失败 | AttackOptions 没配、冷却标签名错、或超出 MaxRange | 检查 14.6 节点配置 |
| 编译报 C2664：`AppendTags` 参数无法从 `const FGameplayTagContainer *` 转换 | `GetCooldownTags()` 返回指针，`AppendTags` 需要引用 | 按 8.2 修改：先判空再用 `*CooldownTags` 解引用传入 |
| 编译报 C2065："Trigger" 未声明的标识符 | 声明行用了旧类型名 `FGameplayAbilityTrigger`（5.6 已改名），声明失败导致变量不存在 | 把 `FGameplayAbilityTrigger` 改成 `FAbilityTriggerData`（枚举 `EGameplayAbilityTriggerSource` 不变） |
| 编译报 C2065："CooldownTags" 未声明的标识符 | UE 5.6 把基类 `UGameplayAbility::CooldownTags` 移除了，能力里直接写 `CooldownTags.AddTag(...)` 找不到成员 | 在能力**头文件**里自声明 `FGameplayTagContainer CooldownTags;` 并覆写 `virtual const FGameplayTagContainer* GetCooldownTags() const override;`，cpp 里返回 `&CooldownTags`（手册 10 / 11 已改） |
| 编译报 C2664：`ApplyEffectSpecHandleToTarget` 参数 1 无法从 const 转换 | `FGameplayEventData.Target` 是 `const AActor*`，函数需要 `AActor*` | 用 `const_cast<AActor*>(Payload.Target.Get())` 传入（手册 10 / 11 已改） |
| 编译警告 C4996：`AbilityTags` 已弃用 | UE 5.6 把 `UGameplayAbility::AbilityTags` 改为私有，要求用 `SetAssetTags`（构造函数里） | 删掉 `AbilityTags.AddTag(...)` 行，保留后面的 `SetAssetTags(AssetTags)`；本册三处（DrawSword / NormalAttack / ThreeCombo）都已改 |
| 编译报"找不到 XX" | 类被先引用但还没创建 | 按第 15 节闸门顺序执行 |

---

## 18. 后续扩展（第二册）

1. **特殊技能：跳跃劈砍**：距离 > 阈值时强制选中，根运动位移（D7）拉近距离；选招任务加"特殊情况优先"规则；
2. **招式数据资产化**：把伤害、距离、冷却从代码/行为树配置迁到 `UBossAttackData` 数据资产（决策书 8.2）；
3. **三距离对峙（D27/D28）**：攻击后按距离进入后退/侧移/靠近；
4. **重攻击 / 更多招式**：继续往 AttackOptions 里加选项即可。
