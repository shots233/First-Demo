# BOSS 四连霸体斩击实现指导

> 招式代号：**FourCombo（四连斩）**。基于 `UGA_Boss_ThreeCombo` 骨架克隆，
> 新增**霸体窗口**机制：四段挥砍期间 BOSS 不可被普通受击与弹反打断，
> 但**破韧照旧直接打断**、**死亡压过一切**。动画资产由作者本人在编辑器中填充。

---

## 一、设计决策（已拍板）

| # | 决策点 | 口径 |
| --- | --- | --- |
| D1 | 弹反 | 弹反照常生效（玩家侧收益不变），BOSS 韧性照常削减；但四连期间弹反**不触发僵直、不打断招式** |
| D2 | 破韧 | 韧性被打空 → 照旧直接打断四连斩（走现有 Executable 链路） |
| D3 | 死亡 | 血量归零时死亡能力压过霸体，取消四连斩（现有框架天然满足） |
| D4 | 出招条件 | 与三连斩一致：`FBossAttackOption`（距离段/权重/冷却），首版参数照抄三连斩 |
| D5 | 普通受击 | 四段挥砍期间不开放受击窗口；**第四段后摇保留 ANS_HitReactWindow 作为玩家惩罚窗口** |

### 为什么破韧/死亡不需要写任何新代码

- 破韧：`ABossCharacter::ApplyPoiseDamage` → `Boss_Event_PoiseBroken` → `UGA_Boss_Executable`
  激活时 `CancelAbilities(Boss.Ability.Attack)`（GA_Boss_Executable.cpp:65-72）。
  四连斩的 AssetTags 挂了 `Boss.Ability.Attack` 父标签，自动被取消。→ **D2 免费获得**。
- 死亡：`UGA_Boss_Death` 由 `Shared_Status_Dead` 标签出现自动触发（OwnedTagAdded），
  激活后 `CancelAllAbilities`，且它没有任何 ActivationBlockedTags。→ **D3 免费获得**。
- 结论：霸体只需要拦两条路径——**普通受击**（靠不开受击窗口 + 标签双保险）
  和**弹反僵直**（靠 ParryStagger 内部分支）。

---

## 二、新增 GameplayTag（4 个）

`Source/First/Public/MyGameplayTags.h`（加在 Boss_Status_HitReactWindow 附近）：

```cpp
// BOSS 霸体窗口：四连斩等招式期间存在；期间弹反不触发僵直、受击不打断。
FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Status_Uninterruptible);
// 四连斩身份与冷却。
FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Ability_Attack_FourCombo);
FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Cooldown_Attack_FourCombo);
```

`Source/First/Private/MyGameplayTags.cpp`：

```cpp
UE_DEFINE_GAMEPLAY_TAG(Boss_Status_Uninterruptible, "Boss.Status.Uninterruptible");
UE_DEFINE_GAMEPLAY_TAG(Boss_Ability_Attack_FourCombo, "Boss.Ability.Attack.FourCombo");
UE_DEFINE_GAMEPLAY_TAG(Boss_Cooldown_Attack_FourCombo, "Boss.Cooldown.Attack.FourCombo");
```

> 命名沿用现有层级：`Boss.Ability.Attack.*` 保证被 Executable/ParryStagger 的
> `CancelAbilities(Boss.Ability.Attack)` 覆盖（这是 D2 破韧打断的前提）。

---

## 三、新增 ANS_SuperArmorWindow（霸体窗口通知）

新文件 `Public/Notifies/ANS_SuperArmorWindow.h` + `Private/Notifies/ANS_SuperArmorWindow.cpp`，
完整照抄 `ANS_HitReactWindow` 的结构（DisplayName 改为 `"Super Armor Window"`）。

与 HitReactWindow 的两点差异（规避上次审查发现的"无计数双重移除"隐患）：

```cpp
// 头文件私有成员：记录本窗口是否真的加过标签。
bool bTagAddedByThisWindow = false;

// NotifyBegin：
//   1. 取 Owner 的 ASC（判空）；
//   2. 死亡门控：HasMatchingGameplayTag(Shared_Status_Dead) 时不加；
//   3. AddLooseGameplayTag(Boss_Status_Uninterruptible, 1) 后 bTagAddedByThisWindow = true。

// NotifyEnd：
//   只有 bTagAddedByThisWindow 为 true 才 RemoveLooseGameplayTag(..., 1)，
//   然后复位标志。蒙太奇被打断时引擎会补发 NotifyEnd（bTriggerOnEnd 默认 true），
//   即使漏发，GA_Boss_FourCombo::EndAbility 还有兜底清理（见第五节）。
```

---

## 四、修改 GA_Boss_ParryStagger（弹反霸体分支，D1 核心）

`Private/AbilitySystem/Abilities/BOSS/GA_Boss_ParryStagger.cpp::ActivateAbility`，
在 `CommitAbility` 相关判空之后、**现有 CancelAbilities 之前**插入：

```cpp
// 霸体分支：四连斩等霸体招式期间，弹反照常削韧，但不取消招式、不进僵直。
// 注意：不能用 ActivationBlockedTags 拦整个能力——削韧逻辑就在本能力内部，
// 拦掉激活等于弹反不削韧，违反 D1。
const bool bBossUninterruptible =
    ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Uninterruptible);

if (bBossUninterruptible)
{
    const float PoiseDamage = TriggerEventData ? FMath::Max(TriggerEventData->EventMagnitude, 0.f) : 50.f;
    AActor* PoiseSource = TriggerEventData ? const_cast<AActor*>(TriggerEventData->Instigator.Get()) : nullptr;
    const EFirstPoiseDamageResult PoiseResult = Boss->ApplyPoiseDamage(PoiseDamage, PoiseSource);

    // 破韧：Boss_Event_PoiseBroken → Executable 会同步取消包括本能力在内的攻击链
    //（D2 打断路径）。与主流程同样先检查生命周期再退出。
    if (!IsActive() || PoiseResult != EFirstPoiseDamageResult::Reduced)
    {
        FinishParryStagger(false);
        return;
    }

    // 未破韧：静默结束——不取消四连斩、不播僵直蒙太奇、不挂 Staggered。
    FinishParryStagger(false);
    return;
}
```

要点：

1. **不动现有非霸体主流程**（CancelAbilities → ApplyPoiseDamage → 僵直），只在入口分流；
2. 破韧在霸体分支内**照常发生**：`ApplyPoiseDamage` 返回 Broken 时，
   同步激活的 Executable 会取消四连斩——这正是 D2 要的行为；
3. `FinishParryStagger` 已有 `IsActive()` 防御，重复调用安全。

---

## 五、修改 GA_Boss_HitReact（双保险）

构造函数加一行：

```cpp
// 霸体期间即使动画里误放了受击窗口，也不触发受击打断（双保险；
// 正常情况四连斩前四段根本不开放受击窗口）。
ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Uninterruptible);
```

> 因为霸体用的是**动画窗口通知**（不是 GA 全程 OwnedTag），第四段后摇的
> ANS_HitReactWindow 不受影响，惩罚窗口（D5）照常工作。

---

## 六、新增 GA_Boss_FourCombo（克隆 ThreeCombo）

新文件 `Public/AbilitySystem/Abilities/BOSS/GA_Boss_FourCombo.h` +
`Private/AbilitySystem/Abilities/BOSS/GA_Boss_FourCombo.cpp`，
以 `UGA_Boss_ThreeCombo` 为模板，差异如下：

### 构造函数

```cpp
FGameplayTagContainer AssetTags;
AssetTags.AddTag(MyGameplayTags::Boss_Ability_Attack);            // 破韧/弹反统一取消的父标签
AssetTags.AddTag(MyGameplayTags::Boss_Ability_Attack_FourCombo);  // 身份标签（BT 按它激活）
SetAssetTags(AssetTags);

ActivationRequiredTags.AddTag(MyGameplayTags::Boss_Status_WeaponDrawn);
ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Staggered);
ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);
ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Attacking);
ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Executable);
ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_BeingExecuted);
ActivationOwnedTags.AddTag(MyGameplayTags::Boss_Status_Attacking);

CooldownGameplayEffectClass = UFirstGE_BossCooldown::StaticClass();
CooldownTags.AddTag(MyGameplayTags::Boss_Cooldown_Attack_FourCombo);
CooldownDuration = 8.f;   // 首版对齐三连斩（D4），后续在 GA 默认值里调

bIsCancelable = true;     // 注意：这不意味着"可被打断"——
                          // CancelAbilities 只响应破韧/死亡等系统级取消，
                          // 玩家普攻与弹反的中断路径已被第四、五节拦下。
```

### ActivateAbility / EndAbility / HandleMeleeHit

与 ThreeCombo 逐行对应，仅替换数据源：

| ThreeCombo | FourCombo |
| --- | --- |
| `Boss->GetThreeComboMontage()` | `Boss->GetFourComboMontage()` |
| `Boss->GetThreeComboDefenseData()` | `Boss->GetFourComboDefenseData()` |
| `Boss->GetThreeComboDamagePerHit()` | `Boss->GetFourComboDamagePerHit()` |
| MontageTask 槽名 `"ThreeComboMontage"` | `"FourComboMontage"` |

`EndAbility` 保持 ThreeCombo 的三件事：`EndAttackWarping()`、
`ToggleWeaponCollision(false)`，并**新增霸体兜底清理**：

```cpp
// 兜底：蒙太奇被打断且引擎漏发 NotifyEnd 时，确保霸体标签不残留。
// SetCount 0 幂等，不会把别处加的计数抹成负数。
if (UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponentFromActorInfo())
{
    ASC->SetLooseGameplayTagCount(MyGameplayTags::Boss_Status_Uninterruptible, 0);
}
```

> 沿用 `GA_Boss_RetreatChargedSlash.cpp:253` 对 HitReactWindow 的同款兜底写法，
> 全项目霸体来源只有四连斩一处，SetCount(0) 无误伤面。

---

## 七、修改 ABossCharacter（三组新字段，全部 BP 可调）

`Public/Character/BossCharacter.h`，紧邻三连斩对应字段放置：

```cpp
// 四连斩：单个完整四连蒙太奇（动画本身包含四段挥砍，四个 Section）。
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Anim", meta=(AllowPrivateAccess="true"))
TObjectPtr<UAnimMontage> FourComboMontage;

FORCEINLINE UAnimMontage* GetFourComboMontage() const { return FourComboMontage; }

// 四连斩每段基础伤害。
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Combat", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
float FourComboDamagePerHit = 20.f;   // 首版略低于三连的 22，四段总量更高

FORCEINLINE float GetFourComboDamagePerHit() const { return FourComboDamagePerHit; }

// 四连斩的防御白名单与资源伤害（ParryPoiseDamage 首版 50，对齐其它招式）。
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Combat|Defense", meta=(AllowPrivateAccess="true"))
FFirstMeleeDefenseData FourComboDefenseData;

FORCEINLINE const FFirstMeleeDefenseData& GetFourComboDefenseData() const { return FourComboDefenseData; }
```

`Private/Character/BossCharacter.cpp` 构造函数中，对齐现有三招的初始化：

```cpp
FourComboDefenseData.ParryPoiseDamage = 50.f;
```

---

## 八、行为树与数据资产配置（编辑器操作）

1. **DA_BossStartUpData**：`AbilitiesToGive` 数组追加 `GA_Boss_FourCombo`；
2. **BT_Boss → BTTask_BossSelectAttack 的 AttackOptions**：复制三连斩那一行，改：
   - `AbilityTag = Boss.Ability.Attack.FourCombo`
   - `CooldownTag = Boss.Cooldown.Attack.FourCombo`
   - `MaxRange / MinRange / SelectionWeight`：首版照抄三连斩（D4）；
   - `RequiredRetreatSpace = 0`（四连斩无后撤需求）；
3. **BP_Boss 类默认值**：
   - `FourComboMontage` ← 作者填充的 `AM_Boss_Attack_FourCombo`（见第九节）；
   - `FourComboDamagePerHit`、`FourComboDefenseData` 按需微调；
   - `GA_Boss_FourCombo` 的 Motion Warping 参数（`AttackWarpingData`）照抄三连斩首版。

---

## 九、动画资产填充清单（作者执行）

新建蒙太奇 `AM_Boss_Attack_FourCombo`（骨架与现有 BOSS 蒙太奇一致），要求：

1. **四个 Section** 顺序衔接：`FourCombo1 → FourCombo2 → FourCombo3 → FourCombo4`
   （对齐 `AM_Boss_Attack_Combo` 的三连模式；Section 自动 Next 即可，GA 只播一次）；
2. 每段攻击有效帧放**武器碰撞通知**（照抄三连斩蒙太奇里的摆法：
   开关碰撞 + 发 `DK.Event.MeleeHit` 的 Notify）；
3. **前四段挥砍期间**放 `ANS_SuperArmorWindow`（本次新增，DisplayName "Super Armor Window"）：
   建议从第一段起手帧覆盖到第四段命中帧结束；
4. **第四段后摇**放 `ANS_HitReactWindow`（惩罚窗口，D5）——注意与霸体窗口**不重叠**；
5. 按需摆放 `ANS_SlashTrail`（拖影）与 `AnimNotify_PlaySFX`（音效）；
6. 若某段需要吸附玩家，Motion Warping 窗口照三连斩的间隔摆放（GA 侧共享一个 Warp 会话，已支持多窗口）。

---

## 十、验证清单（编辑器内逐项过）

| # | 场景 | 预期 |
| --- | --- | --- |
| V1 | 四连期间玩家普攻命中 BOSS | 掉血、飘受击特效，但招式不中断、无受击动画 |
| V2 | 四连期间玩家弹反成功（第 1~3 段任意段） | 弹反音效/玩家收益正常；BOSS 韧性条下降；招式继续 |
| V3 | 四连期间弹反把韧性打空 | 四连被立刻打断，进入 Executable 可处决流程（D2） |
| V4 | 四连期间 BOSS 血量被打空 | 立刻中断进入死亡流程（D3） |
| V5 | 第四段后摇内玩家命中 | 触发受击打断（惩罚窗口生效，D5） |
| V6 | 四连结束后检查标签 | `showdebug abilitysystem`：Uninterruptible / Attacking 均已消失 |
| V7 | 冷却 | 打完一轮后 8 秒内 BT 不再选中四连斩 |
| V8 | 四连期间玩家被第 2 段命中且未防御 | 正常掉血/受击（招式对玩家的判定不受霸体影响） |

---

## 十一、已知边界与提醒

1. **顺序假设**：ParryStagger 霸体分支中 `ApplyPoiseDamage` 若破韧，Executable 在同一
   调用栈内同步取消本能力——沿用主流程已验证的 `IsActive()` 检查模式，勿删；
2. **受击事件仍会发送**：四连期间玩家命中仍触发 `Boss_Event_HitReact`（掉血即发），
   只是 HitReact 能力被窗口+标签双重挡住。这是预期行为，不是 bug；
3. 上次审查发现的 `BossCharacter.cpp:383` **Instigator 用错 API** 的 bug 与本功能同链路
   （受击事件），建议顺手修复：改为从属性集伤害事件里取真实攻击者；
4. 霸体窗口是**动画驱动**的：如果作者把窗口摆满整个蒙太奇（含后摇），
   惩罚窗口（D5）会失效——按第九节第 3、4 条摆放。
