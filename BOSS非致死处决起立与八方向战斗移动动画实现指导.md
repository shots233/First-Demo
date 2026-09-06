# BOSS 非致死处决起立与八方向战斗移动动画实现指导

> 适用项目：`First`（UE 5.6 中文版 / C++ / GAS / Behavior Tree）  
> 编写依据：当前工程源码、`BP_Boss`、`ABP_Boss`、现有处决与三距离对峙链路  
> 本册目标：先补齐“处决后 BOSS 存活时从倒地姿势起立”的闭环，再让 BOSS 在近距离战斗中面向玩家播放前、后、左、右及四个斜向移动动画  
> 重要说明：这是一份**实施指导**。创建本文件不代表其中的 C++、蓝图或 UE 资产已经被修改

---

## 0. 完成后的最终效果

本册分成两个阶段，推荐严格按顺序实施。

### 0.1 阶段 A：非致死处决后起立

```text
玩家成功处决
  ↓
BOSS 在精确 Notify 帧受到 AttackPower × 10 的处决伤害
  ├─ Health <= 0
  │    ├─ 保留处决 Target Montage 最后一帧
  │    ├─ 不播放通用死亡 Montage
  │    └─ 绝不播放起立
  │
  └─ Health > 0
       ├─ 玩家先恢复控制
       ├─ BOSS 继续保持 Busy，AI 不移动
       ├─ 播放 AM_Boss_ExecutionGetUp
       ├─ 起立期间不可再次处决
       └─ 起立完成后回满 Poise，恢复行为树
```

### 0.2 阶段 B：八方向战斗移动

```text
巡逻
  → 朝移动方向
  → 前向 Walk

远距离追击（玩家在周旋距离之外）
  → 朝路径移动方向
  → 前向 Run

进入近距离战斗
  → BOSS 胸口保持面向玩家
  ├─ 靠近玩家 → 向前走
  ├─ 后退拉开 → 向后走
  ├─ 左右周旋 → 左走 / 右走
  └─ NavMesh 产生斜向速度 → 对应斜向走

攻击、拔剑、受击、弹反僵直、处决、死亡
  → 进入 Frozen，冻结 CharacterMovement 的自动转向
  → 由现有 Montage 独立控制表现
```

### 0.3 为什么先做起立

当前处决不是必杀，而是：

```text
处决伤害 = 玩家当前 AttackPower × 10
```

因此 BOSS 存活是正常玩法分支。当前代码在玩家处决 Montage 完成后直接结束
`GA_Boss_Executable`，并停止 BOSS 的 Target Montage；如果 Target 最后一帧是倒地，BOSS
会直接混回站立 Locomotion。起立不是单纯锦上添花，而是现有处决功能缺少的收尾。

八方向移动属于高频表现升级，但它依赖朝向模式、动画参数和 Blend Space 三部分；应在
处决闭环稳定后单独实施，排查会更清楚。

---

## 1. 当前工程已确认的事实

### 1.1 BOSS 当前实际骨骼

当前 `BP_Boss` 使用的是男骑士，不是旧手册中的女骑士：

| 项目 | 当前实际内容 |
|---|---|
| BOSS Mesh | `/Game/Dark_Knight/Dark_Knight_Male/Meshes/SKM_DKM_Full` |
| BOSS Skeleton | `SK_DKM_Full` |
| BOSS AnimBP | `/Game/Enemy/AnimBP/ABP_Boss` |
| AnimBP 父类 | `FirstDKAnimInstance` |
| 全身 Montage Slot | `DefaultSlot` |
| 现有 Locomotion | `/Game/Enemy/AnimBP/BS_Boss_Locomotion`，一维速度混合 |

因此本册统一使用：

```text
Anim_DKM_*
SKM_DKM_Full
SK_DKM_Full
```

不要再给当前 BOSS 选择 `Anim_DKF_* / SK_DKF_Full`。

### 1.2 本册覆盖旧移动手册的哪些内容

旧文件《BOSS待机与移动动画装配实现指导.md》记录的是较早工程状态。遇到冲突时以本册
和当前源码为准。

| 旧说明 | 当前结论 |
|---|---|
| BOSS 是 `DKF` 女骑士 | 当前是 `DKM` 男骑士 |
| 八方向以后再考虑 | 本册正式接入八方向战斗步行 |
| `BS_Boss_Locomotion` 可选 | 当前已有一维 Blend Space；本册另建二维资产，不直接破坏旧资产 |
| 巡逻/追击二选一速度服务 | 当前已经是三距离战斗逻辑，不能照抄旧服务代码 |
| 只靠 `GroundSpeed` 即可 | 八方向还需要角色本地移动方向 |

旧手册中以下原则仍然有效：

- AnimGraph 最终输出必须经过 `DefaultSlot`；
- Locomotion 使用 In-Place 动画，不让动画根位移和 `Move To` 位移叠加；
- 改动 `UPROPERTY`、原生类或原生 Gameplay Tag 后关闭编辑器做完整 Build。

### 1.3 当前可直接使用的男骑士移动动画

```text
/Game/Dark_Knight/Dark_Knight_Male/Animations/Anim_DKM_Idle_Alert

/Game/Dark_Knight/Dark_Knight_Male/Animations/Anim_DKM_Walk_Alert_Fwd
/Game/Dark_Knight/Dark_Knight_Male/Animations/Anim_DKM_Walk_Alert_FwdLeft
/Game/Dark_Knight/Dark_Knight_Male/Animations/Anim_DKM_Walk_Alert_Left
/Game/Dark_Knight/Dark_Knight_Male/Animations/Anim_DKM_Walk_Alert_BwdLeft
/Game/Dark_Knight/Dark_Knight_Male/Animations/Anim_DKM_Walk_Alert_Bwd
/Game/Dark_Knight/Dark_Knight_Male/Animations/Anim_DKM_Walk_Alert_BwdRight
/Game/Dark_Knight/Dark_Knight_Male/Animations/Anim_DKM_Walk_Alert_Right
/Game/Dark_Knight/Dark_Knight_Male/Animations/Anim_DKM_Walk_Alert_FwdRight

/Game/Dark_Knight/Dark_Knight_Male/Animations/Anim_DKM_Run_Alert_Fwd
/Game/Dark_Knight/Dark_Knight_Male/Animations/Anim_DKM_Run_Alert_FwdLeft
/Game/Dark_Knight/Dark_Knight_Male/Animations/Anim_DKM_Run_Alert_FwdRight
```

`Walk_Alert` 有完整八方向，`Run_Alert` 只有前、前左、前右。因此本册采用：

```text
近距离战斗 = 八方向 Walk
远距离追击 = 前向 Run
```

### 1.4 当前可用于起立的源动画

```text
/Game/Katana_Animations/Animations/Sequence/08_Hit/06_Get_Up/AS_Get_Up_F_Seq
/Game/Katana_Animations/Animations/Sequence/08_Hit/06_Get_Up/AS_Get_Up_B_Seq
```

它们不能直接给 `SK_DKM_Full` 使用。工程资产引用已确认，这两条 Katana 动画的源骨骼是：

```text
/Game/Katana_Animations/Demo/Mannequin/Character/Mesh/UE4_Mannequin_Skeleton
```

因此推荐直接通过现有：

```text
/Game/Katana_Animations/Ik/RTG_Man_DK
```

重定向到 BOSS 骨架。

项目里的 `/Game/IK/RTG_BOSS` 也以男骑士为目标，但它的源 IK Rig 面向
`SK_DKMannequin`，更适合已经先转到玩家 DK Mesh 的中间资产。这里直接从 Katana 原始
Mannequin 动画出发，使用 `RTG_Man_DK` 路径更短，也与现有处决 Target 指导一致。

当前处决 Target 相关资产是：

```text
/Game/Enemy/AnimBP/Montages/AM_Boss_ExecutionTarget_01
/Game/Enemy/AnimBP/Source/AS_Execution_Target_02_Seq1
```

注意：Montage 名字写着 `_01`，内部实际使用的是 `Target_02` 重定向序列。选择
`Get_Up_F` 还是 `Get_Up_B` 时必须看实际末帧姿势，不能只根据文件名猜。

### 1.5 与现有处决指导的关系

本册建立在《主角格挡、弹反、破防与BOSS韧性处决实现指导.md》已经完成的基础上，
只增量修改当前代码：

- 保留原指导第 18～20 节的可处决、配对处决和 `AttackPower × 10` 伤害链；
- 保留“处决致死不播放通用死亡 Montage”的特殊死亡分流；
- 覆盖旧版 `HandleExecutionFinished()` 直接 `FinishExecutable(false)` 的做法；
- 覆盖旧版“非致死后立即停止 Target 并回 Locomotion”的时机；
- 不改变五秒可处决窗口、ApplyDamage Notify 或玩家 Execute Ability。

还要区分两个完全不同的“恢复”：

```text
ExecutableMontage 的 Recover
  = 玩家没有处决、弱点状态超时后的恢复动作（当前仍未实现）

AM_Boss_ExecutionGetUp
  = 处决伤害已经结算、BOSS 存活后的倒地起立（本册实现）
```

不要把新起立动画塞进旧 `ExecutableMontage` 的 `Recover` Section。

---

## 2. 本册采用的设计原则

### 2.1 首版起立不新建 Ability，也不新建 Gameplay Tag

起立继续留在现有：

```text
UGA_Boss_Executable
```

生命周期里。

从处决开始直到起立完成，继续持有：

```text
Boss.Status.Executable
Boss.Status.Staggered
Boss.Status.BeingExecuted
```

这样可以同时保证：

- `BTService_UpdateBossState` 继续把 `bIsBusy` 写成 `true`；
- 行为树不会在 BOSS 还躺着时重新 `Move To`；
- 玩家 `FindExecutableBoss()` 会因为 `BeingExecuted` 仍存在而拒绝二次处决；
- 起立完成后只走一次现有 `EndAbility()`，集中清标签和恢复 Poise。

以后如果“击倒、倒地追击、起身攻击”等系统也要复用起立，再拆出
`GA_Boss_GetUp + Boss.Status.Recovering`。当前首版先避免两个 Ability 交接产生竞态。

### 2.2 致死与非致死必须在起立前分流

```text
bBossKilledByExecution == true 或 Shared.Status.Dead 存在
  → 不起立

处决伤害已经生效，并且 BOSS 仍活着
  → 起立
```

不要仅凭 `bExecutionDamageApplied` 判断起立。这个变量只表示伤害已经结算，并不表示 BOSS
仍活着。

### 2.3 胶囊碰撞继续由玩家处决 Ability 管理

当前双方 `Pawn` 碰撞是在玩家 `FirstGA_DKExecute` 中保存和恢复的：

- BOSS 存活：玩家 Ability 结束时恢复原碰撞响应；
- BOSS 死亡：保持忽略 Pawn，避免尸体和玩家互相推挤。

本册不要再让 `GA_Boss_Executable` 修改胶囊碰撞。否则双方 Ability 同时恢复同一个状态，
容易互相覆盖。

BOSS 起立阶段只负责：

- 保持忙碌标签；
- 停止角色与 AI 移动；
- 保持武器命中盒关闭；
- 起立完成后释放行为树。

### 2.4 AI 决定去哪里，AnimBP 只读取最终真实速度

不要把“左移、右移、后退”写成动画专用黑板键。NavMesh 可能为了绕障碍改变实际行进
方向，黑板里的目标方向不一定等于角色这一帧的真实速度。

本册统一使用：

```text
Behavior Tree / Move To → 决定目标位置
AI Focus + 朝向模式      → 决定 BOSS 看向谁
实际 Velocity           → 决定播放哪一个方向动画
```

### 2.5 `Boss.Status.Strafing` 暂不作为动画条件

项目虽然声明了：

```text
Boss.Status.Strafing
```

但当前没有能力、任务或服务真正授予它。直接拿它驱动 AnimBP 会导致条件永远为 false。
本册不依赖该标签。

---

## 3. 改动总览

本册不创建新的 C++ 类，只修改现有类，并新建三个 UE 动画资产。

### 3.1 本册涉及的现有 C++ 类

下面这些类已经存在。不要在 Unreal Editor 的“新建 C++ 类”向导里重新创建它们；
只需要按后文找到并修改对应文件。

| 现有类 | 父类 | 头文件 | 源文件 |
|---|---|---|---|
| `ABossCharacter` | `AEnemyCharacter` | `Source/First/Public/Character/BossCharacter.h` | `Source/First/Private/Character/BossCharacter.cpp` |
| `UGA_Boss_Executable` | `UFirstBossGameplayAbility` | `Source/First/Public/AbilitySystem/Abilities/BOSS/GA_Boss_Executable.h` | `Source/First/Private/AbilitySystem/Abilities/BOSS/GA_Boss_Executable.cpp` |
| `UFirstDKAnimInstance` | `UFirstBaseAnimInstance` | `Source/First/Public/AnimInstances/FirstDKAnimInstance.h` | `Source/First/Private/AnimInstances/FirstDKAnimInstance.cpp` |
| `UBTService_UpdateBossState` | `UBTService` | `Source/First/Public/AI/BTService_UpdateBossState.h` | `Source/First/Private/AI/BTService_UpdateBossState.cpp` |
| `UBTService_UpdateMoveSpeed` | `UBTService` | `Source/First/Public/AI/Services/UBTService_UpdateMoveSpeed.h` | `Source/First/Private/AI/Services/UBTService_UpdateMoveSpeed.cpp` |
| `ABossAIController` | `AAIController` | `Source/First/Public/Controller/BossAIController.h` | `Source/First/Private/Controller/BossAIController.cpp` |

> 本册中的“新建”只针对动画序列、Montage 和 Blend Space 等 UE 资产，不代表新建 C++ 类。

### 3.2 文件与资产改动清单

| 文件/资产 | 操作 | 内容 |
|---|---|---|
| `BossCharacter.h` | 修改 | 新增起立 Montage；新增三态旋转模式；可选新增 125 战斗步速 |
| `BossCharacter.cpp` | 修改 | 实现朝移动方向、面向目标、冻结三种旋转模式 |
| `GA_Boss_Executable.h/.cpp` | 修改 | 非致死处决后播放起立，覆盖完成/中断/超时/死亡竞态 |
| `FirstDKAnimInstance.h/.cpp` | 修改 | 新增相对 BOSS 朝向的前后、左右方向参数及步行动画播放速率 |
| `BTService_UpdateBossState.cpp` | 修改 | 近距离战斗开启面向玩家，忙碌/死亡时关闭 |
| `UBTService_UpdateMoveSpeed.cpp` | 可选修改 | 将巡逻 250、近战 125、远追 500 分开 |
| `BossAIController.cpp` | 小幅修改 | 丢失目标时立即冻结残留路径转向 |
| `AS_Boss_ExecutionGetUp` | 新建 UE 资产 | 从 F/B 起立源动画中选择并重定向 |
| `AM_Boss_ExecutionGetUp` | 新建 UE 资产 | 非致死处决后的起立 Montage |
| `BS_Boss_CombatWalk_8D` | 新建 UE 资产 | DKM 八方向战斗步行 Blend Space |
| `ABP_Boss` | 修改 UE 资产 | Walk 状态接入二维 Blend Space |
| `BP_Boss` | 配置 UE 资产 | 填写 GetUp Montage、距离、速度和旋转参数 |
| `BT_Boss` | 只读检查 | 确认忙碌装饰器能中止 `Move To`；`Allow Strafe` 不是本方案的必改项 |

---

# 第一部分：非致死处决后的起立

## 4. 第一步：准备 BOSS 起立动画

### 4.1 先确认处决末帧朝向

1. 在内容浏览器打开：

   ```text
   /Game/Enemy/AnimBP/Montages/AM_Boss_ExecutionTarget_01
   ```

2. 把时间轴拖到最后；
3. 观察 BOSS 最终是：
   - 背部贴地、脸朝上；
   - 还是胸口贴地、脸朝下；
4. 分别预览：

   ```text
   AS_Get_Up_F_Seq
   AS_Get_Up_B_Seq
   ```

5. 选择首帧姿势与 Target 最后一帧最接近的一条。

这里的 `F / B` 可能描述倒地方向，也可能描述起立方向，不同动画包命名约定不完全一致。
最终以预览姿势是否连续为准。

### 4.2 使用现有 RTG_Man_DK 重定向

1. 打开 `/Game/Katana_Animations/Ik/RTG_Man_DK`；
2. 确认预览中的目标角色是男骑士 `SKM_DKM_Full`；
3. 切到 **资产浏览器（Asset Browser）**；
4. 搜索并选中刚才确认的 `AS_Get_Up_F_Seq` 或 `AS_Get_Up_B_Seq`；
5. 预览动画，重点检查：
   - 手脚没有严重扭曲；
   - 膝盖方向正常；
   - 起立结束时双脚落地；
   - Root 没有突然跳到远处；
6. 点击 **导出选中的动画（Export Selected Animations）**；
7. 输出目录选择：

   ```text
   /Game/Enemy/AnimBP/Source/
   ```

8. 建议命名：

   ```text
   AS_Boss_ExecutionGetUp
   ```

不要修改 Katana 动画包的原始资产。

### 4.3 创建起立 Montage

1. 右键 `AS_Boss_ExecutionGetUp`；
2. 选择 **创建 → 创建动画蒙太奇（Create AnimMontage）**；
3. 移动到：

   ```text
   /Game/Enemy/AnimBP/Montages/
   ```

4. 命名：

   ```text
   AM_Boss_ExecutionGetUp
   ```

5. 打开 Montage，设置：

| 选项 | 推荐值 |
|---|---|
| Slot | `DefaultSlot` |
| 是否循环 | 否 |
| Rate Scale | `1.0` |
| Blend In | `0.05 ~ 0.10` |
| Blend Out | `0.15` 左右 |
| Enable Auto Blend Out | 开启 |

Target Montage 仍要保持：

```text
Enable Auto Blend Out = false
```

因为处决致死时要保留 Target 最后一帧。只有新建的 GetUp Montage 开启自动混出。

### 4.4 根运动要求

首版建议让起立保持 In-Place：

- 在重定向副本上检查 **Root Motion**；
- 必要时勾选 **Force Root Lock（强制根锁定）**；
- 不要让起立动画把角色胶囊从处决点推走；
- 不要修改原始 Katana 动画。

后面的 AbilityTask 还会把动画 Root Motion Translation Scale 设为 `0`，作为第二层保险。

### 4.5 资产闸门

暂时不要改代码，先确认：

- `AS_Boss_ExecutionGetUp` 的 Skeleton 是 `SK_DKM_Full`；
- `AM_Boss_ExecutionGetUp` 能在男骑士预览窗口正常播放；
- 动画首帧能接上 Target 末帧；
- 动画末帧是正常站立姿势；
- Montage 不循环并开启 Auto Blend Out。

---

## 5. 第二步：在 ABossCharacter 保存起立 Montage

文件：

```text
Source/First/Public/Character/BossCharacter.h
```

在现有 `ExecutionTargetMontage` 后新增：

```cpp
	// 非致死处决演出结束后，从倒地姿势起立。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category="Boss|Anim|Execution",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> ExecutionGetUpMontage;

	FORCEINLINE UAnimMontage* GetExecutionGetUpMontage() const
	{
		return ExecutionGetUpMontage;
	}
```

不需要修改 `BossCharacter.cpp`，也不需要新建 C++ 类。

---

## 6. 第三步：扩展 GA_Boss_Executable.h

文件：

```text
Source/First/Public/AbilitySystem/Abilities/BOSS/GA_Boss_Executable.h
```

这个类已经存在：

```text
类：UGA_Boss_Executable
父类：UFirstBossGameplayAbility
```

本步骤只修改现有文件，不要通过“新建 C++ 类”向导重新创建它。

### 6.1 新增两个动态回调

在 `private:` 区域、现有事件回调附近新增：

```cpp
	UFUNCTION()
	void HandleExecutionGetUpCompleted();

	UFUNCTION()
	void HandleExecutionGetUpInterrupted();
```

之所以加 `UFUNCTION()`，是因为 `AbilityTask_PlayMontageAndWait` 的动态委托需要绑定
UFunction。

### 6.2 新增起立工具函数与状态

继续在 `private:` 新增：

```cpp
	void StartExecutionGetUp();
	void HandleExecutionGetUpSafetyTimeout();
```

在现有 Bool 成员附近新增：

```cpp
	bool bExecutionGetUpStarted = false;
```

这个 Bool 的职责只有一个：防止完成、取消、Montage 中断和安全计时器同时触发多次
`EndAbility()`。

---

## 7. 第四步：扩展 GA_Boss_Executable.cpp

文件：

```text
Source/First/Private/AbilitySystem/Abilities/BOSS/GA_Boss_Executable.cpp
```

### 7.1 新增 include

在其它 AbilityTask include 附近新增：

```cpp
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
```

### 7.2 激活时重置起立状态

在 `ActivateAbility()` 当前这组初始化代码中：

```cpp
	bExecutionStarted = false;
	bExecutionDamageApplied = false;
	bBossKilledByExecution = false;
	bPlayerTerminalEventHandled = false;
	bOwnsBeingExecutedTag = false;
```

新增：

```cpp
	bExecutionGetUpStarted = false;
```

### 7.3 整体替换 HandleExecutionFinished

```cpp
void UGA_Boss_Executable::HandleExecutionFinished(FGameplayEventData Payload)
{
	if (!bExecutionStarted || bExecutionGetUpStarted)
	{
		return;
	}

	// 玩家 Montage 已经完成。玩家可以先恢复控制，BOSS 独自播放起立。
	bPlayerTerminalEventHandled = true;

	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo();

	if (Boss)
	{
		Boss->GetWorldTimerManager().ClearTimer(ExecutionSafetyTimerHandle);
	}

	if (!bExecutionDamageApplied)
	{
		UE_LOG(LogTemp, Error,
			TEXT("Boss execution finished without Boss.Event.Execution.ApplyDamage"));
		ReturnToExecutableAfterAbort();
		return;
	}

	if (!Boss || !ASC)
	{
		FinishExecutable(true);
		return;
	}

	const bool bBossDead = ASC->HasMatchingGameplayTag(
		MyGameplayTags::Shared_Status_Dead);

	// 致死处决或外部伤害已经把 BOSS 杀死：保留死亡链，绝不起立。
	if (bBossKilledByExecution || bBossDead)
	{
		FinishExecutable(false);
		return;
	}

	StartExecutionGetUp();
}
```

### 7.4 整体替换 HandleExecutionAborted

正常流程走 `ExecutionFinished`。这个分支负责处理玩家 Montage 在伤害前或伤害后异常中断。

```cpp
void UGA_Boss_Executable::HandleExecutionAborted(FGameplayEventData Payload)
{
	if (!bExecutionStarted || bExecutionGetUpStarted)
	{
		return;
	}

	bPlayerTerminalEventHandled = true;

	// 伤害前中断：允许回到剩余的可处决窗口。
	if (!bExecutionDamageApplied)
	{
		ReturnToExecutableAfterAbort();
		return;
	}

	UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo();
	const bool bBossDead = !ASC || ASC->HasMatchingGameplayTag(
		MyGameplayTags::Shared_Status_Dead);

	// 伤害已经结算且 BOSS 存活：本次处决已经消费，仍然进入起立。
	if (!bBossKilledByExecution && !bBossDead)
	{
		StartExecutionGetUp();
		return;
	}

	FinishExecutable(true);
}
```

### 7.5 新增 StartExecutionGetUp

```cpp
void UGA_Boss_Executable::StartExecutionGetUp()
{
	if (!IsActive() || bExecutionGetUpStarted)
	{
		return;
	}

	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo();

	if (!Boss || !ASC)
	{
		FinishExecutable(true);
		return;
	}

	if (bBossKilledByExecution ||
		ASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead))
	{
		FinishExecutable(false);
		return;
	}

	UAnimMontage* GetUpMontage = Boss->GetExecutionGetUpMontage();
	if (!GetUpMontage)
	{
		// 缺少表现资产时不能永久锁住玩法；记录错误后退回旧版直接站立。
		UE_LOG(LogTemp, Warning,
			TEXT("ExecutionGetUpMontage is not configured on BP_Boss"));
		FinishExecutable(false);
		return;
	}

	StopBossMovement();
	if (UBossCombatComponent* Combat = Boss->GetBossCombatComponent())
	{
		// 非攻击状态的安全默认值始终是关闭，而不是重新打开。
		Combat->ToggleWeaponCollision(false);
	}

	bExecutionGetUpStarted = true;
	ExecutingPlayer.Reset();

	UAbilityTask_PlayMontageAndWait* GetUpTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			TEXT("BossExecutionGetUpMontage"),
			GetUpMontage,
			1.f,
			NAME_None,
			true,
			0.f);

	if (!GetUpTask)
	{
		bExecutionGetUpStarted = false;
		FinishExecutable(false);
		return;
	}

	GetUpTask->OnCompleted.AddDynamic(
		this,
		&ThisClass::HandleExecutionGetUpCompleted);
	GetUpTask->OnInterrupted.AddDynamic(
		this,
		&ThisClass::HandleExecutionGetUpInterrupted);
	GetUpTask->OnCancelled.AddDynamic(
		this,
		&ThisClass::HandleExecutionGetUpInterrupted);

	// 复用旧的安全计时器：进入起立前，处决阶段的旧计时器已经清除。
	Boss->GetWorldTimerManager().ClearTimer(ExecutionSafetyTimerHandle);
	Boss->GetWorldTimerManager().SetTimer(
		ExecutionSafetyTimerHandle,
		this,
		&ThisClass::HandleExecutionGetUpSafetyTimeout,
		FMath::Max(GetUpMontage->GetPlayLength() + 0.5f, 0.5f),
		false);

	GetUpTask->ReadyForActivation();
}
```

`CreatePlayMontageAndWaitProxy()` 最后的 `0.f` 是 Root Motion Translation Scale，表示不让
起立 Montage 推动角色胶囊。Montage 资产仍应设置 Force Root Lock，两层保险并不冲突。

本册要求 GetUp Montage 的 Rate Scale 保持 `1.0`，因此安全计时器可以直接使用
`GetPlayLength() + 0.5`。以后若允许自定义播放速率，超时时间也要按播放速率换算。

### 7.6 新增完成、中断和超时回调

```cpp
void UGA_Boss_Executable::HandleExecutionGetUpCompleted()
{
	if (!IsActive() || !bExecutionGetUpStarted)
	{
		return;
	}

	if (ABossCharacter* Boss = GetBossCharacterFromActorInfo())
	{
		Boss->GetWorldTimerManager().ClearTimer(ExecutionSafetyTimerHandle);
	}

	bExecutionGetUpStarted = false;
	FinishExecutable(false);
}

void UGA_Boss_Executable::HandleExecutionGetUpInterrupted()
{
	if (!IsActive() || !bExecutionGetUpStarted)
	{
		return;
	}

	if (ABossCharacter* Boss = GetBossCharacterFromActorInfo())
	{
		Boss->GetWorldTimerManager().ClearTimer(ExecutionSafetyTimerHandle);
	}

	bExecutionGetUpStarted = false;
	FinishExecutable(true);
}

void UGA_Boss_Executable::HandleExecutionGetUpSafetyTimeout()
{
	if (!IsActive() || !bExecutionGetUpStarted)
	{
		return;
	}

	UE_LOG(LogTemp, Error,
		TEXT("Boss execution get-up montage timed out"));

	// 先清 Bool，EndAbility 停止 Montage 时即使触发 Interrupted 也不会重复结束。
	bExecutionGetUpStarted = false;
	FinishExecutable(true);
}
```

### 7.7 修改旧的处决安全超时

当前 `HandleExecutionSafetyTimeout()` 在伤害已经生效时会直接结束 Ability。为了避免异常
超时路径再次出现“倒地瞬间站起”，把该分支改成存活时启动起立：

```cpp
void UGA_Boss_Executable::HandleExecutionSafetyTimeout()
{
	if (!bExecutionStarted || bExecutionGetUpStarted)
	{
		return;
	}

	if (bExecutionDamageApplied)
	{
		NotifyExecutingPlayerAbort();

		UFirstAbilitySystemComponent* ASC =
			GetFirstAbilitySystemComponentFromActorInfo();
		const bool bBossDead = !ASC || ASC->HasMatchingGameplayTag(
			MyGameplayTags::Shared_Status_Dead);

		if (!bBossKilledByExecution && !bBossDead)
		{
			StartExecutionGetUp();
			return;
		}

		FinishExecutable(false);
		return;
	}

	UE_LOG(LogTemp, Error,
		TEXT("Boss execution timed out before ApplyDamage/terminal event"));
	NotifyExecutingPlayerAbort();
	ReturnToExecutableAfterAbort();
}
```

### 7.8 ReturnToExecutableAfterAbort 补充状态归零

在该函数现有 Bool 重置区域新增：

```cpp
	bExecutionGetUpStarted = false;
```

理论上起立开始后不会回到“重新可处决”分支。这一行是为了避免以后改动时遗留状态。

### 7.9 整体替换 EndAbility

```cpp
void UGA_Boss_Executable::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	ABossCharacter* Boss = GetBossCharacterFromActorInfo();

	// 必须先停安全计时器、关闭起立回调门，再停止 Montage。
	if (Boss)
	{
		Boss->GetWorldTimerManager().ClearTimer(ExecutionSafetyTimerHandle);
	}
	bExecutionGetUpStarted = false;

	const bool bMustReleaseExecutingPlayer =
		bExecutionStarted && !bPlayerTerminalEventHandled;

	if (bMustReleaseExecutingPlayer)
	{
		NotifyExecutingPlayerAbort();
	}

	RemoveBeingExecutedTag();

	UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo();
	const bool bBossDead = ASC && ASC->HasMatchingGameplayTag(
		MyGameplayTags::Shared_Status_Dead);
	UAnimInstance* AnimInstance = Boss && Boss->GetMesh()
		? Boss->GetMesh()->GetAnimInstance()
		: nullptr;

	// 只有本次处决伤害真正致死时，才保留 Target Montage 最后一帧。
	if (AnimInstance && Boss && !bBossKilledByExecution)
	{
		if (UAnimMontage* WeakMontage = Boss->GetExecutableMontage())
		{
			AnimInstance->Montage_Stop(0.15f, WeakMontage);
		}
		if (UAnimMontage* TargetMontage = Boss->GetExecutionTargetMontage())
		{
			AnimInstance->Montage_Stop(0.15f, TargetMontage);
		}
		if (UAnimMontage* GetUpMontage = Boss->GetExecutionGetUpMontage())
		{
			AnimInstance->Montage_Stop(0.1f, GetUpMontage);
		}
	}

	if (Boss)
	{
		if (UBossCombatComponent* Combat = Boss->GetBossCombatComponent())
		{
			Combat->ToggleWeaponCollision(false);
		}

		if (!bBossDead)
		{
			Boss->RestorePoiseToFull();
		}
	}

	ExecutingPlayer.Reset();

	Super::EndAbility(
		Handle,
		ActorInfo,
		ActivationInfo,
		bReplicateEndAbility,
		bWasCancelled);
}
```

不要在这里调用：

```cpp
Combat->ToggleWeaponCollision(true);
```

攻击 Ability 的武器碰撞 Notify 才有资格打开命中盒。非攻击状态的安全默认值必须是
关闭。

也不要在这里强制：

```cpp
SetMovementMode(MOVE_Walking)
```

当前 Executable Ability 从未 `DisableMovement()`，只是停止当前移动请求。起立完成后
`Executable / Staggered / BeingExecuted` 被清理，下一次行为树 Tick 会自然恢复移动。

---

## 8. 第五步：完整 Build 与 BP_Boss 配置

本阶段修改了 `UPROPERTY` 和 Ability C++，推荐：

1. 保存代码；
2. 关闭 UE 编辑器；
3. 编译 `FirstEditor / Win64 / Development`；
4. 编译成功后重新打开项目。

打开：

```text
/Game/Enemy/BP_Boss
```

在 **类默认值（Class Defaults）** 中找到：

```text
Boss | Anim | Execution
  Execution Get Up Montage
```

设置为：

```text
AM_Boss_ExecutionGetUp
```

编译并保存 `BP_Boss`。

### 8.1 检查已放置的 Update Boss State 服务

打开 `BT_Boss`，选中当前实际运行的 `Update Boss State` 服务节点，在细节面板确认
`Action Blocking Tags` 至少包含：

```text
Boss.Status.Attacking
Boss.Status.DrawSword
Boss.Status.Staggered
Boss.Status.Executable
Boss.Status.BeingExecuted
```

C++ 构造函数中的默认容器不会保证覆盖行为树里已经保存过的旧节点实例。若这里漏掉
`Executable / BeingExecuted`，即使起立 Ability 仍活跃，黑板 `bIsBusy` 也可能提前变回
false，AI 会在起立动画中开始 `Move To`。

不需要修改：

- `GA_Boss_Death`；
- `MyGameplayTags.h/.cpp`；
- `DA_BossStartUpData`；
- 玩家 `FirstGA_DKExecute`；
- 行为树黑板键。

---

## 9. 阶段 A 分步验收

### 9.1 先测非致死处决

把 BOSS 血量和玩家攻击力调成处决后明显不会死亡的组合，例如：

```text
BOSS Health = 500
玩家 AttackPower = 15
处决倍率 = 10
最终处决伤害 = 150
```

预期：

1. 伤害帧后 BOSS 剩余 350 血；
2. 玩家 Montage 结束后先恢复控制；
3. BOSS 从倒地姿势播放起立；
4. 起立期间行为树 `bIsBusy = true`；
5. 起立期间再次按处决键无效；
6. BOSS 不移动、不旋转、不产生武器伤害；
7. 起立完成后 Poise 回满，BOSS 恢复战斗。

### 9.2 再测致死处决

让 BOSS 剩余生命小于本次处决伤害。

预期：

- 不播放 `AM_Boss_ExecutionGetUp`；
- 不切到通用 `AM_Boss_Death`；
- 保留 Target Montage 最后一帧；
- 尸体不会重新站起；
- 行为树保持死亡状态。

### 9.3 测试起立中死亡

允许玩家在 BOSS 起立时进行普通攻击，并把 BOSS 打死。

预期：

- GetUp Montage 被停止；
- 进入普通 Death Montage；
- 不继续起立；
- 死亡时不恢复 Poise；
- 不重复通知玩家处决中止。

### 9.4 测试错误配置兜底

依次测试：

| 场景 | 预期 |
|---|---|
| `ExecutionGetUpMontage` 留空 | 输出 Warning，但不会永久 Busy |
| Montage 骨架错误 | 播放失败后结束 Ability，不永久定格 |
| GetUp 被其它 Ability 取消 | 走 Interrupted，正确释放状态 |
| 故意把 GetUp 做成循环 | 安全计时器最终释放状态并输出 Error |
| 连按处决键 | 起立期间不能再次处决 |

---

# 第二部分：面向玩家的八方向战斗移动

## 10. 第六步：先处理距离与速度数据

### 10.1 统一周旋半径

当前源码默认值不一致：

```text
ABossCharacter::StrafeRadius                   = 250
UBTService_UpdateCombatDistance::StrafeRadius  = 500
```

侧移/后退任务使用角色上的 `StrafeRadius` 计算落点，距离服务却使用自己的
`StrafeRadius` 判断分区。如果一个是 250、一个是 500，BOSS 会在边界附近反复切换行为，
八方向动画也会跟着抖动。

首版最简单的处理：

1. 把 `BossCharacter.h` 中默认值改为：

   ```cpp
   float StrafeRadius = 500.f;
   ```

2. 打开 `BP_Boss → 类默认值 → Boss | Patrol`；
3. 把 `Strafe Radius` 明确设为 `500`；
4. 打开 `BT_Boss`，选中 `Update Combat Distance` 服务；
5. 把它的 `Strafe Radius` 也设为 `500`。

更长期的低耦合方案，是让 `UpdateCombatDistance` 直接读取
`Boss->GetStrafeRadius()`，只保留一个数据源；但它不是本册动画接入的强制内容。

### 10.2 可选：把巡逻 250、近战 125、远追 500 分开

当前 `UBTService_UpdateMoveSpeed` 只有：

```text
远追 = ChaseMoveSpeed 500
其余 = PatrolMoveSpeed 250
```

也就是说，如果直接把 `PatrolMoveSpeed` 改成 125，巡逻也会一起变成 125。

如果你确定“只有周旋、后退、近距离靠近使用 125”，建议在 `BossCharacter.h` 的移动速度
附近新增：

```cpp
	// 进入近距离战斗后，靠近、后退和周旋共用的谨慎步速。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category="Boss|Patrol",
		meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float CombatMoveSpeed = 125.f;
```

然后把：

```text
Source/First/Private/AI/Services/UBTService_UpdateMoveSpeed.cpp
```

中最后的速度选择改成：

```cpp
	float TargetSpeed = Boss->PatrolMoveSpeed;

	if (bChasing)
	{
		TargetSpeed = bTooFar
			? Boss->ChaseMoveSpeed
			: Boss->CombatMoveSpeed;
	}

	Boss->SetMovementSpeed(TargetSpeed);
```

最终为：

| 状态 | 速度 |
|---|---:|
| 未索敌巡逻 | 250 |
| 玩家在周旋距离外，远追 | 500 |
| 近距离靠近 | 125 |
| 左右周旋 | 125 |
| 贴脸后退 | 125 |

`125` 适合沉稳、观察感较强的 BOSS。方向逻辑正确后，如果脚步显得过慢，可以先试
`140 ~ 170`，不要为了修脚滑开启 Root Motion。

---

## 11. 第七步：给 FirstDKAnimInstance 增加本地方向

本册不创建 `BossAnimInstance` 新类。继续扩展现有：

```text
类：UFirstDKAnimInstance
父类：UFirstBaseAnimInstance
头文件：Source/First/Public/AnimInstances/FirstDKAnimInstance.h
源文件：Source/First/Private/AnimInstances/FirstDKAnimInstance.cpp
```

主角 AnimBP 不使用这些新变量时不会受到影响。

### 11.1 修改 FirstDKAnimInstance.h

在现有 `GroundSpeed / bHasAcceleration / bShouldMove` 附近新增：

```cpp
	// 实际移动方向相对角色朝向的前后分量：+1 前，-1 后。
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly,
		Category="AnimData|Locomotion")
	float LocalForwardDirection = 0.f;

	// 实际移动方向相对角色朝向的左右分量：+1 右，-1 左。
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly,
		Category="AnimData|Locomotion")
	float LocalRightDirection = 0.f;

	// 给 Walk Blend Space Player 使用，125 速度约为 0.5，250 速度约为 1.0。
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly,
		Category="AnimData|Locomotion")
	float WalkAnimationPlayRate = 1.f;

	// DKM Walk 动画按 250 作为首版参考速度；可在 AnimBP 类默认值中微调。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category="AnimData|Locomotion",
		meta=(ClampMin="1.0"))
	float WalkReferenceSpeed = 250.f;
```

这里使用 `-1 ~ 1` 的方向值，而不是把 Blend Space 直接绑定到 `125 / 250` 的世界速度。
这样以后调周旋速度不需要重新摆放所有方向采样点。

### 11.2 修改 NativeThreadSafeUpdateAnimation

当前这个函数里已经包含主角锁定时的 `MoveForward / MoveRight` 计算。
这段逻辑必须保留，否则完成本册后会破坏主角现有的锁定移动动画。

为避免初学时漏掉代码，可以把 `FirstDKAnimInstance.cpp` 中的
`NativeThreadSafeUpdateAnimation()` 整体替换为下面这个**合并后的完整版本**。
它同时包含：

1. BOSS 使用实际速度计算八方向参数；
2. 主角原有的锁定移动参数计算。

文件开头现有的这两个包含也必须保留：

```cpp
#include "Character/DKCharacter.h"
#include "Components/Targeting/DKTargetLockComponent.h"
```

```cpp
void UFirstDKAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);

	if (!OwningCharacter || !OwningMovementComponent)
	{
		return;
	}

	const FVector WorldVelocity = OwningCharacter->GetVelocity();
	const FVector HorizontalVelocity(
		WorldVelocity.X,
		WorldVelocity.Y,
		0.f);

	GroundSpeed = HorizontalVelocity.Size();

	if (GroundSpeed > 3.f)
	{
		// 世界速度转换成相对角色朝向的本地速度：X=前后，Y=左右。
		const FVector LocalVelocity =
			OwningCharacter->GetActorTransform()
				.InverseTransformVectorNoScale(HorizontalVelocity);

		const FVector2D LocalDirection =
			FVector2D(LocalVelocity.X, LocalVelocity.Y).GetSafeNormal();

		LocalForwardDirection = LocalDirection.X;
		LocalRightDirection = LocalDirection.Y;
	}
	else
	{
		LocalForwardDirection = 0.f;
		LocalRightDirection = 0.f;
	}

	WalkAnimationPlayRate = GroundSpeed > 3.f
		? FMath::Clamp(
			GroundSpeed / FMath::Max(WalkReferenceSpeed, 1.f),
			0.5f,
			1.25f)
		: 1.f;

	bHasAcceleration =
		OwningMovementComponent->GetCurrentAcceleration().SizeSquared2D() > 0.f;

	// 保持现有规则，避免本册改变主角原来的状态机行为。
	bShouldMove = GroundSpeed > 3.f && bHasAcceleration;

	// 以下是项目现有的主角锁定移动逻辑，必须继续保留。
	// 它使用玩家输入方向；上面的 LocalForward/Right 使用实际速度，二者用途不同。
	MoveForward = 0.f;
	MoveRight = 0.f;
	bIsTargetLocked = false;

	const ADKCharacter* DKCharacter = Cast<ADKCharacter>(OwningCharacter);
	if (DKCharacter)
	{
		bIsTargetLocked =
			DKCharacter->GetTargetLockComponent() &&
			DKCharacter->GetTargetLockComponent()->IsTargetLocked();

		if (bIsTargetLocked)
		{
			const FVector InputVector =
				OwningCharacter->GetLastMovementInputVector();

			if (!InputVector.IsNearlyZero())
			{
				const FVector InputFlat =
					FVector(InputVector.X, InputVector.Y, 0.f).GetSafeNormal();

				const FVector Forward =
					OwningCharacter->GetActorForwardVector();
				const FVector Right =
					OwningCharacter->GetActorRightVector();

				MoveForward = FMath::Clamp(
					FVector::DotProduct(InputFlat, Forward),
					-1.f,
					1.f);

				MoveRight = FMath::Clamp(
					FVector::DotProduct(InputFlat, Right),
					-1.f,
					1.f);
			}
		}
	}
}
```

变量含义示例：

| BOSS 行为 | Forward | Right |
|---|---:|---:|
| 面向玩家向前靠近 | `+1` | `0` |
| 面向玩家向后拉开 | `-1` | `0` |
| 面向玩家向自身右侧横移 | `0` | `+1` |
| 面向玩家向自身左侧横移 | `0` | `-1` |
| 向右前方移动 | 约 `+0.707` | 约 `+0.707` |

这里不使用 `CalculateDirection`，因此无需修改 `First.Build.cs` 或添加 `AnimGraphRuntime`
模块。

---

## 12. 第八步：给 ABossCharacter 增加三态旋转模式

### 12.1 修改 BossCharacter.h

在 `UCLASS()` 上方新增枚举：

```cpp
UENUM(BlueprintType)
enum class EBossRotationMode : uint8
{
	OrientToMovement UMETA(DisplayName="朝移动方向"),
	FaceTarget       UMETA(DisplayName="面向目标"),
	Frozen           UMETA(DisplayName="冻结自动旋转")
};
```

三种模式分别负责：

| 模式 | 使用场景 | CharacterMovement 自动旋转 |
|---|---|---|
| `OrientToMovement` | 巡逻、远距离追击 | 朝实际移动方向 |
| `FaceTarget` | 近战靠近、后退、周旋 | 朝 AIController 的 Focus |
| `Frozen` | 攻击、拔剑、受击、弹反、处决、起立、死亡 | 两套自动旋转都关闭 |

然后在 `public:` 区域新增：

```cpp
	UFUNCTION(BlueprintCallable, Category="Boss|Movement")
	void SetBossRotationMode(EBossRotationMode NewMode);

	UFUNCTION(BlueprintPure, Category="Boss|Movement")
	EBossRotationMode GetBossRotationMode() const
	{
		return BossRotationMode;
	}
```

在 `private:` 区域新增：

```cpp
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly,
		Category="Boss|Movement",
		meta=(AllowPrivateAccess="true"))
	EBossRotationMode BossRotationMode =
		EBossRotationMode::OrientToMovement;
```

### 12.2 修改 BossCharacter.cpp 构造函数

把当前直接设置：

```cpp
GetCharacterMovement()->bOrientRotationToMovement = true;
```

改为：

```cpp
	bUseControllerRotationYaw = false;
	SetBossRotationMode(EBossRotationMode::OrientToMovement);
```

### 12.3 在 BossCharacter.cpp 新增实现

```cpp
void ABossCharacter::SetBossRotationMode(EBossRotationMode NewMode)
{
	BossRotationMode = NewMode;

	// 始终让 CharacterMovement 按 RotationRate 平滑旋转。
	bUseControllerRotationYaw = false;

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		// 先关闭两套自动转向，再只开启当前模式需要的一套。
		Movement->bOrientRotationToMovement = false;
		Movement->bUseControllerDesiredRotation = false;

		switch (NewMode)
		{
		case EBossRotationMode::OrientToMovement:
			Movement->bOrientRotationToMovement = true;
			break;

		case EBossRotationMode::FaceTarget:
			Movement->bUseControllerDesiredRotation = true;
			break;

		case EBossRotationMode::Frozen:
		default:
			// 两者保持 false：CharacterMovement 不再自动改变 Yaw。
			break;
		}
	}
}
```

`Frozen` 只冻结 **CharacterMovement 的自动转向**。它不会禁止 Montage Root Motion，也不会
拦截 `SetActorRotation()`；因此现有 `BossAlert` 任务仍可显式让 BOSS 转向玩家。

为什么不能在忙碌时简单调用 `SetBossRotationMode(OrientToMovement)`：只要残留
`Acceleration / RequestedVelocity` 还存在，BOSS 就可能在攻击或受击开始的一瞬间转向路径。
`Frozen` 才是真正互斥的第三种状态。

为什么不能只在 AIController 上 `SetFocus(Player)`：

```text
bOrientRotationToMovement = true
```

时，角色移动组件仍会让 BOSS 朝速度方向转身。结果是 BOSS 走到玩家侧面，但身体也转向
侧面，看起来仍是“正面走路”，不会形成真正的横移。

---

## 13. 第九步：在 UpdateBossState 统一选择旋转模式

文件：

```text
Source/First/Private/AI/BTService_UpdateBossState.cpp
```

### 13.1 修改 include 与角色类型

把：

```cpp
#include "Character/BaseCharacter.h"
```

改成：

```cpp
#include "Character/BossCharacter.h"
```

把：

```cpp
ABaseCharacter* Boss = Cast<ABaseCharacter>(AIController->GetPawn());
```

改成：

```cpp
ABossCharacter* Boss = Cast<ABossCharacter>(AIController->GetPawn());
```

### 13.2 在写入 bIsBusy 后新增旋转模式判断

在现有：

```cpp
BB->SetValueAsBool(TEXT("bIsBusy"), bIsBusy);
```

后新增：

```cpp
	AActor* Target = Cast<AActor>(
		BB->GetValueAsObject(TEXT("TargetActor")));
	const bool bShouldChase =
		BB->GetValueAsBool(TEXT("bShouldChase"));
	const bool bPlayerTooFar =
		BB->GetValueAsBool(TEXT("bPlayerTooFar"));

	EBossRotationMode DesiredRotationMode =
		EBossRotationMode::OrientToMovement;

	if (bIsDead || bIsBusy)
	{
		DesiredRotationMode = EBossRotationMode::Frozen;

		// Frozen 只管旋转；这里再终止残留寻路，防止忙碌 Montage 仍被速度带着滑行。
		AIController->StopMovement();
	}
	else if (IsValid(Target) &&
		bShouldChase &&
		!bPlayerTooFar)
	{
		DesiredRotationMode = EBossRotationMode::FaceTarget;
	}

	Boss->SetBossRotationMode(DesiredRotationMode);

	if (DesiredRotationMode == EBossRotationMode::FaceTarget &&
		AIController->GetFocusActor() != Target)
	{
		AIController->SetFocus(
			Target,
			EAIFocusPriority::Gameplay);
}
```

这里的 `StopMovement()` 是忙碌状态的安全兜底，不能代替行为树上
`bIsBusy / bIsDead` 装饰器的 `Observer Aborts = Both`。装饰器负责及时中止分支，服务负责
清掉可能残留的一次寻路请求。

最终朝向规则：

| 状态 | 旋转模式 | 表现 |
|---|---|---|
| 巡逻 | `OrientToMovement` | 朝移动方向前走 |
| 初次警戒 | `OrientToMovement` | 现有 `BossAlert` 可继续显式转向 |
| 500 外远追 | `OrientToMovement` | 朝路径方向前跑 |
| 500 内靠近 | `FaceTarget` | 面向玩家前走 |
| 左右周旋 | `FaceTarget` | 面向玩家横移 |
| 贴脸后退 | `FaceTarget` | 面向玩家倒退 |
| 攻击/拔剑/受击/弹反 | `Frozen` | 停止寻路并冻结自动转向，Montage 独立表现 |
| 可处决/被处决/起立 | `Frozen` | 保持静止 |
| 死亡 | `Frozen` | 不再由 CharacterMovement 旋转 |

不需要新增黑板键。

### 13.3 目标丢失时立即关闭

文件：

```text
Source/First/Private/Controller/BossAIController.cpp
```

在 `HandleTargetPerceptionUpdated()` 的丢失目标分支、`ClearFocus()` 附近新增：

```cpp
			if (ABossCharacter* Boss = GetBossCharacter())
			{
				// 先冻结丢失目标瞬间可能残留的路径转向；
				// UpdateBossState 下一 Tick 会在空闲状态恢复 OrientToMovement。
				Boss->SetBossRotationMode(EBossRotationMode::Frozen);
			}
```

状态服务下一次 Tick 本来也会关闭，这里是为了避免丢失目标后的短暂延迟。

---

## 14. 第十步：完整 Build

到这里已经修改：

- `BossCharacter.h/.cpp`；
- `FirstDKAnimInstance.h/.cpp`；
- `BTService_UpdateBossState.cpp`；
- 可选的 `UBTService_UpdateMoveSpeed.cpp`；
- `BossAIController.cpp`。

关闭 UE 编辑器，完成一次 `FirstEditor / Win64 / Development` Build。编译成功后重新打开
项目，再创建 Blend Space。

如果只使用 Live Coding，动画蓝图里可能暂时看不到新变量：

```text
LocalForwardDirection
LocalRightDirection
WalkAnimationPlayRate
```

---

## 15. 第十一步：创建八方向 Blend Space

### 15.1 新建资产

1. 打开：

   ```text
   /Game/Enemy/AnimBP/
   ```

2. 右键空白处；
3. 选择 **动画（Animation）→ 混合空间（Blend Space）**；
4. 注意：选择普通 **Blend Space**，不要选择 **Blend Space 1D**；
5. Skeleton 选择 `SK_DKM_Full`；
6. 命名：

   ```text
   BS_Boss_CombatWalk_8D
   ```

不要直接覆盖旧 `BS_Boss_Locomotion`。保留旧资产，出现问题时可以快速回退。

### 15.2 设置两条轴

打开 Blend Space，在 **资产细节（Asset Details）→ 轴设置（Axis Settings）** 中设置：

```text
水平轴 X
  Name = LocalForwardDirection   （前向：+1 前，-1 后）
  Minimum Axis Value = -1.0
  Maximum Axis Value =  1.0

垂直轴 Y
  Name = LocalRightDirection     （右向：+1 右，-1 左）
  Minimum Axis Value = -1.0
  Maximum Axis Value =  1.0
```

> 轴约定与主角锁定移动的 `BS_DK_LockedWalk/LockedRun` 保持一致：**X=前后，Y=左右**。
> 如果当前资产仍按旧版反着建（X=Right，Y=Forward），交换轴设置后必须按下面的样本表重新摆放所有样本，并同步对调 ABP 里的接线（见 16.2）——不要只改轴名称而保留旧样本位置。

轴平滑时间先设置：

```text
0.08 ~ 0.12 秒
```

首轮只使用这一层轴平滑，不要同时叠加过多权重平滑，以免方向反应迟钝。

### 15.3 先放四方向和中心点

先只放下面五个点：

| 动画 | X：Forward | Y：Right |
|---|---:|---:|
| `Anim_DKM_Idle_Alert` | `0` | `0` |
| `Anim_DKM_Walk_Alert_Fwd` | `1` | `0` |
| `Anim_DKM_Walk_Alert_Right` | `0` | `1` |
| `Anim_DKM_Walk_Alert_Bwd` | `-1` | `0` |
| `Anim_DKM_Walk_Alert_Left` | `0` | `-1` |

在右下角预览输入中逐个输入：

```text
(1, 0)   前走
(0, 1)   右走
(-1, 0)  后退
(0, -1)  左走
```

确认前后左右没有接反，再继续添加斜向。

### 15.4 再补四个斜向

单位方向的 45° 分量约为：

```text
0.707
```

添加：

| 动画 | X：Forward | Y：Right |
|---|---:|---:|
| `Anim_DKM_Walk_Alert_FwdRight` | `0.707` | `0.707` |
| `Anim_DKM_Walk_Alert_BwdRight` | `-0.707` | `0.707` |
| `Anim_DKM_Walk_Alert_BwdLeft` | `-0.707` | `-0.707` |
| `Anim_DKM_Walk_Alert_FwdLeft` | `0.707` | `-0.707` |

保存 Blend Space。

### 15.5 左右命名反了怎么办

本册定义：

```text
LocalRightDirection > 0 = 角色自身右侧
LocalRightDirection < 0 = 角色自身左侧
```

如果游戏里身体动作和移动方向相反，先交换 Blend Space 中 Left/Right 动画的位置，不要
立即修改通用 C++ 方向公式。动画包的 L/R 有时描述“身体受力方向”而不是“移动方向”。

---

## 16. 第十二步：接入 ABP_Boss

打开：

```text
/Game/Enemy/AnimBP/ABP_Boss
```

### 16.1 保留现有状态机结构

推荐继续保留：

```text
Idle
Walk
Run
```

- `Idle`：继续播放 `Anim_DKM_Idle_Alert`；
- `Walk`：改为使用 `BS_Boss_CombatWalk_8D`；
- `Run`：首版继续播放 `Anim_DKM_Run_Alert_Fwd`。

完整八方向 Run 暂时不做，因为当前 DKM 没有左、右、后退跑步资源。远距离追击仍朝移动
方向，因此前向 Run 足够稳定。

### 16.2 在 Walk 状态连接参数

进入 `Walk` 状态：

1. 删除或断开原来的前向 Walk / 旧一维 Blend Space Player；
2. 拖入 `BS_Boss_CombatWalk_8D`；
3. 连接：

   ```text
   LocalForwardDirection → X / LocalForwardDirection
   LocalRightDirection   → Y / LocalRightDirection
   WalkAnimationPlayRate → Play Rate
   ```

如果 Blend Space Player 暂时没有显示 `Play Rate` 引脚：

1. 选中节点；
2. 在细节面板找到 Play Rate；
3. 点击对应属性旁的 **公开为引脚（Expose as Pin）**。

### 16.3 推荐过渡回差

为了避免同一帧同时满足两条离开当前状态的过渡条件，推荐使用下面这组互斥规则：

```text
Idle → Walk   GroundSpeed > 20 AND GroundSpeed <= 375
Walk → Idle   GroundSpeed < 10

Walk → Run    GroundSpeed > 375
Run → Walk    GroundSpeed < 350

Idle → Run    GroundSpeed > 375
```

不要再添加 `Run → Idle` 的直接过渡。BOSS 从高速停止时会先经过：

```text
Run → Walk → Idle
```

这样 `Run → Walk` 与 `Run → Idle` 不会重叠，过渡结果也不依赖状态机连线的优先级。

`125 / 250` 都会进入 Walk，`500` 会进入 Run。

不要用 `bShouldMove` 作为本册唯一移动条件。当前 `bShouldMove` 同时要求存在加速度，AI 在
减速但仍有实际速度时可能提前变成 false。这里以 `GroundSpeed` 过渡更稳定。

### 16.4 检查 DefaultSlot

最终 AnimGraph 必须仍然是：

```text
Output Pose
  ← DefaultSlot
      ← Locomotion 状态机
```

如果删除 `DefaultSlot`，攻击、受击、弹反、处决、起立和死亡 Montage 都可能无法覆盖
Locomotion。

---

## 17. 第十三步：配置 BP_Boss 旋转与速度

打开 `BP_Boss → 类默认值`。

### 17.1 Character Movement

推荐：

```text
Use Controller Rotation Yaw       = false
Orient Rotation To Movement       = true   （出生默认）
Use Controller Desired Rotation   = false  （出生默认）
Rotation Rate → Z (Yaw)           = 540
```

运行时 `SetBossRotationMode()` 会按三种模式切换中间两个选项。不要把
`Use Controller Rotation Yaw` 勾成 true，否则角色会绕过 CharacterMovement 的
`RotationRate`，转向可能显得生硬。

### 17.2 BOSS 移动参数

推荐首版：

```text
Patrol Move Speed = 250
Combat Move Speed = 125   （如果完成 10.2）
Chase Move Speed  = 500
Strafe Radius     = 500
```

若没有完成 10.2，就不会出现 `Combat Move Speed` 属性，近战移动仍使用
`Patrol Move Speed = 250`。这不影响方向系统是否正确，只影响节奏和脚步速度。

编译并保存 `BP_Boss`。

---

## 18. 第十四步：理解 BT_Boss 的 Allow Strafe

本方案**不要求**你为了八方向移动去改 `Allow Strafe`。可以在 `BT_Boss` 中依次查看：

```text
后退分支  → Move To：BackOffLocation
周旋分支  → Move To：StrafeLocation
靠近分支  → Move To：TargetActor
```

这个选项影响的是 PathFollowing 自己写入的 **Move 优先级 Focus**，并不是“启用侧移”：

- `BackOffLocation / StrafeLocation` 是 Vector 黑板键，勾选后也不会自动让 BOSS 面向玩家；
- `TargetActor` 是 Actor 黑板键，勾选后 PathFollowing 才可能用该 Actor 作为 Move Focus；
- 当前项目已用 `SetFocus(Target, EAIFocusPriority::Gameplay)` 写入更高优先级的 Gameplay
  Focus，因此真正让近战横移成立的是 `FaceTarget` 模式 + Gameplay Focus；
- 远追朝路径方向成立的原因是 `OrientToMovement`，也不是 `Allow Strafe = false`。

所以首版可以保持三个 `Move To` 当前的 `Allow Strafe` 值，不必为了本册统一改成 true。
真正需要检查的是：这些移动分支能否在 `bIsBusy / bIsDead` 改变时被中止。

`Update Boss State` 服务间隔建议：

```text
0.05 ~ 0.10 秒
```

不要为动画新增以下黑板键：

```text
MoveDirection
IsMovingLeft
IsMovingRight
```

---

## 19. 阶段 B 分步验收

### 19.1 先在 Blend Space 内验方向

| 输入 | 预期 |
|---|---|
| `(0, 1)` | 前走 |
| `(1, 0)` | 右走 |
| `(0, -1)` | 后退 |
| `(-1, 0)` | 左走 |
| `(0.707, 0.707)` | 前右 |
| `(-0.707, 0.707)` | 前左 |
| `(0.707, -0.707)` | 后右 |
| `(-0.707, -0.707)` | 后左 |

### 19.2 再测朝向模式

| 场景 | 预期 |
|---|---|
| 未发现玩家巡逻 | 朝路径方向前走 |
| 初次警戒 | 现有 BossAlert 转向玩家，不突然横移 |
| 玩家距离 600 以上 | 朝路径方向前跑，不侧跑 |
| 玩家距离 300～450 | 胸口持续面向玩家，左右周旋 |
| 玩家贴脸 | 面向玩家倒退，不转身逃跑 |
| 周旋超时后靠近 | 面向玩家向前步行 |
| 目标丢失 | 短暂 Frozen 清残留，随后恢复朝移动方向巡逻 |
| 玩家离开 NavMesh | 清 Focus，恢复默认朝向 |

最后一项依赖当前项目**已经存在**的
`UBTService_UpdateCombatDistance.cpp` 不可达分支：它会在目标无法投射到 NavMesh 时把
`bShouldChase` 写为 false，并清除 Gameplay Focus。本册没有新建这段逻辑；如果以后删掉或
改写该分支，这条验收预期也必须同步调整。

### 19.3 再测动作互斥

| 场景 | 预期 |
|---|---|
| 侧移中开始攻击 | `Move To` 被中止，不继续横向滑行 |
| 拔剑 | 不一边持续转向一边播放拔剑 |
| 普通受击 | 停止移动，受击 Montage 完整播放 |
| 弹反僵直 | 不继续绕玩家转圈 |
| 可处决 | 停止移动，弱点姿势稳定 |
| 被处决/起立 | 全程不移动、不追踪旋转 |
| 死亡 | 不再移动或旋转 |

### 19.4 最后测绕障碍

把玩家站在柱子或墙角附近，让 NavMesh 产生非直线路径。

预期：

- 动画方向跟随角色真实速度变化；
- 不会因为黑板目标仍在前方而永远只播前走；
- BOSS 胸口在近战模式下大体朝向玩家；
- 路径急转弯时方向经过约 `0.08 ~ 0.12` 秒平滑，不高速闪切。

---

## 20. 联合回归测试清单

| 编号 | 场景 | 预期 |
|---|---|---|
| T01 | 未索敌巡逻 | DKM 前向 Walk，速度 250 |
| T02 | 警戒拔剑 | 不边拔剑边滑步 |
| T03 | 远距离追击 | DKM 前向 Run，速度 500 |
| T04 | 左侧周旋 | 面向玩家，播放 Left/斜向 Left |
| T05 | 右侧周旋 | 面向玩家，播放 Right/斜向 Right |
| T06 | 玩家贴脸 | 面向玩家播放 Bwd |
| T07 | 周旋后靠近 | 面向玩家播放 Fwd |
| T08 | 攻击中 | Locomotion 不覆盖攻击 Montage |
| T09 | 受击/弹反 | 停移动且不继续转向 |
| T10 | 非致死处决 | 倒地后播放 GetUp，再恢复战斗 |
| T11 | 致死处决 | 不播 GetUp，保持 Target 最后一帧 |
| T12 | 起立中被打死 | 停 GetUp，进入普通死亡 |
| T13 | 起立中连续按处决 | 不会重复处决 |
| T14 | GetUp 配置为空 | 不永久 Busy |
| T15 | 目标丢失 | 关闭 Focus 朝向并恢复巡逻 |
| T16 | 玩家离开 NavMesh | 不反复 MoveTo 卡死，不持续朝玩家转 |
| T17 | 重启 PIE | 无 Loose Tag、朝向模式或速度残留 |

---

## 21. 常见问题与排查

### 21.1 非致死处决后仍瞬间站起

按顺序检查：

1. `HandleExecutionFinished()` 是否调用 `StartExecutionGetUp()`；
2. `BP_Boss → Execution Get Up Montage` 是否已填写；
3. GetUp Montage Skeleton 是否为 `SK_DKM_Full`；
4. `ABP_Boss` 是否保留 `DefaultSlot`；
5. 输出日志中是否出现 `ExecutionGetUpMontage is not configured`。

### 21.2 致死处决后尸体反而起立

检查：

- 起立前是否同时判断 `bBossKilledByExecution` 和 `Shared.Status.Dead`；
- `HandleExecutionApplyDamage()` 是否在伤害前临时添加 `Boss.Status.Executed`；
- `GA_Boss_Death` 的处决死亡分支是否仍保留 Target Montage；
- 是否错误地在死亡 Ability 中手动调用了 `StartExecutionGetUp()`。

### 21.3 BOSS 起立后永远不动

检查：

- GetUp Montage 是否意外循环；
- `OnCompleted / OnInterrupted / OnCancelled` 是否全部绑定；
- `ExecutionSafetyTimerHandle` 是否正常设置；
- `Boss.Status.BeingExecuted` 是否在 `EndAbility()` 中移除；
- `Executable / Staggered` 是否随 Ability 结束自动移除；
- 行为树 `bIsBusy` 是否恢复为 false。

### 21.4 起立中仍能打到玩家

检查所有攻击 Montage 的碰撞 Notify 是否被中止，并确认：

```cpp
Combat->ToggleWeaponCollision(false);
```

存在于 `StartExecutionGetUp()` 和 `EndAbility()`。不要在起立结束时主动打开命中盒。

### 21.5 起立时身体漂移或穿地

- 确认使用的是重定向副本，不是直接修改原动画；
- GetUp Animation Sequence 勾选 Force Root Lock；
- AbilityTask 的 Root Motion Translation Scale 保持 `0.f`；
- 检查 Target 末帧与 GetUp 首帧是否真的匹配；
- 检查 Mesh 相对胶囊的 Z 偏移有没有在蓝图里被改动。

### 21.6 BOSS 侧移时仍然转身朝移动方向

检查：

```text
bOrientRotationToMovement      = false（近战运行时）
bUseControllerDesiredRotation = true （近战运行时）
AIController Focus            = TargetActor
```

只 `SetFocus()` 不关闭 `OrientRotationToMovement` 不够；但 `Allow Strafe` 不是这里的必检项。
如果正在攻击、受击或处决，则两个旋转选项都应为 false，对应 `Frozen`。

### 21.7 BOSS 永远只播放前走

- 查看 AnimBP 调试器里的 `LocalForwardDirection / LocalRightDirection`；
- 如果横移时 Right 仍接近 0，检查战斗朝向是否真正开启；
- 如果变量正常，检查 Walk 状态是否真的使用新二维 Blend Space；
- 检查是否误选了旧的 `BS_Boss_Locomotion`；
- 检查 Blend Space X/Y 引脚是否接反。

### 21.8 左右动画完全反了

先交换 Blend Space 中：

```text
Anim_DKM_Walk_Alert_Left
Anim_DKM_Walk_Alert_Right
```

及对应两个斜向位置。不要先改通用本地速度公式。

### 21.9 125 速度下明显滑步

依次处理：

1. 确认 `WalkAnimationPlayRate` 在 125 速度时约为 `0.5`；
2. 确认 Blend Space Player 的 Play Rate 引脚已经连接；
3. 在 `0.45 ~ 0.70` 范围微调最小播放速率；
4. 必要时把 `CombatMoveSpeed` 提到 `140 ~ 170`；
5. 不要开启 Locomotion Root Motion。

### 21.10 进入/离开 500 距离时身体反复转向

这是距离边界没有回差导致的。先确认两个 `StrafeRadius` 都是 500。仍抖动时，再把距离
判定升级成进入/退出不同阈值，例如：

```text
进入近战朝向 <= 480
退出近战朝向 >= 520
```

首版不要同时改距离、行为树顺序和动画，先确定抖动确实发生再加入回差。

### 21.11 周旋时频繁左右切换

当前 `BTTask_BossGetStrafeLocation` 每次执行都会重新 `RandBool()`。如果任务频繁重启，八
方向动画会把左右变化表现得更明显。

后续可把左右方向缓存一个周旋周期；这属于 AI 目标点稳定性问题，不是 Blend Space
方向公式错误。

### 21.12 攻击 Montage 被移动动画盖住

检查 AnimGraph：

```text
Output Pose ← DefaultSlot ← Locomotion
```

并确认攻击、受击、处决、起立 Montage 全部使用 `DefaultSlot`。如果不同 Montage 使用同一
Slot，它们会按播放顺序互相接管，这正是本册从 Target Montage 平滑切换到 GetUp Montage
所依赖的机制。

---

## 22. 推荐实施顺序与编译闸门

不要一次把所有步骤都做完再测试。

```text
阶段 A1：预览 Target 末帧，选择 F/B 起立源动画
  ↓
阶段 A2：RTG_Man_DK 重定向，创建 AM_Boss_ExecutionGetUp
  ↓
资产闸门：在男骑士预览窗口单独播放正确
  ↓
阶段 A3：BossCharacter 添加 GetUp Montage 属性
  ↓
阶段 A4：GA_Boss_Executable 接入起立生命周期
  ↓
完整 Build
  ↓
阶段 A5：BP_Boss 填 Montage，分别测试非致死/致死/起立中死亡
  ↓
起立闭环通过后再进入阶段 B
  ↓
阶段 B1：统一 StrafeRadius；可选拆分 125 CombatMoveSpeed
  ↓
阶段 B2：FirstDKAnimInstance 增加本地方向参数
  ↓
阶段 B3：BossCharacter + UpdateBossState 接入三态旋转模式
  ↓
完整 Build
  ↓
阶段 B4：创建 BS_Boss_CombatWalk_8D
  ↓
阶段 B5：ABP_Boss 先接四方向并测试
  ↓
阶段 B6：补四个斜向
  ↓
阶段 B7：确认 BT_Boss 忙碌装饰器能中止 Move To
  ↓
完整联合回归测试
```

---

## 23. 本册刻意不做的内容

为了先得到稳定闭环，本册暂不加入：

- 独立的 `GA_Boss_GetUp`；
- 独立的 `Boss.Status.Recovering`；
- 起立攻击、假起身或多段起身分支；
- 倒地期间处决第二阶段；
- 起立期间专用无敌 Gameplay Tag；
- Motion Warping 起立校正；
- 完整八方向奔跑；
- Stride Warping、Distance Matching、Foot IK；
- 侧移左右方向的跨任务持久缓存；
- 480/520 距离回差；
- 绕障碍时的高级朝向预测。

这些都能建立在本册的 GetUp 生命周期、三态旋转接口和实际本地速度参数上，不需要推翻
首版结构。

---

## 24. 最终职责总结

```text
GA_Boss_Executable
  ├─ 管理可处决、被处决、非致死起立的完整生命周期
  ├─ 决定致死时保留 Target，存活时播放 GetUp
  └─ 起立完成后统一清状态、回 Poise

GA_Boss_Death
  ├─ 普通死亡播放 Death Montage
  └─ 处决致死保留 Target 最后一帧

ABossCharacter
  ├─ 保存动画资产和三档速度
  └─ 提供 OrientToMovement / FaceTarget / Frozen 三态旋转接口

BTService_UpdateBossState
  ├─ 聚合 bIsBusy
  └─ 决定近战时面向玩家、忙碌或死亡时冻结自动旋转

Behavior Tree
  └─ 只决定去哪里，不决定动画方向

UFirstDKAnimInstance
  └─ 把真实世界速度转换成相对角色的前后/左右方向

ABP_Boss + BS_Boss_CombatWalk_8D
  └─ 根据本地方向选择 DKM 八方向步行动画
```

这套结构不会把 AI 决策、角色朝向、GAS 生命周期和动画采样塞进同一个类：

- 行为树负责目标与分支；
- GAS 负责动作互斥与处决生命周期；
- Character 负责角色级移动模式；
- AnimInstance 负责只读动画参数；
- AnimBP 负责最终视觉混合。

因此后续扩展起身攻击、完整跑步方向或更高级的脚步匹配时，都有明确接入位置。
