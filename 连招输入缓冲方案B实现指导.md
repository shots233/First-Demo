# 连招输入缓冲（方案 B）实现指导

> 适用项目：`First`（UE 5.6 / C++ / GAS / Enhanced Input）  
> 本文只提供实现指导，不会自动修改你的 C++ 或动画资源。  
> 本文是“连招手感不顺畅”问题的方案 B：**持久输入监听 + 窗口结束前全段输入缓冲**。

---

## 1. 为什么要做方案 B

你现在的连招输入链路是：

```text
ComboWindow 打开 → 创建 WaitInputPress
→ 只有窗口内的按键才会被缓存（bWantsNextCombo = true）
→ 窗口关闭 → 若已缓存 → 切下一段
```

问题在于：

```text
窗口打开之前按的键 → 任务还没创建 → 按键丢失
窗口关闭之后按的键 → 任务已结束 → 按键丢失
```

所以玩家必须**精确地**在窗口内按键，才会觉得“要狂按才能接上、还容易夹断”。

方案 B 的思路：

```text
输入监听从“本段一开始”就存在，直到本段结束才销毁。
窗口只是一个“截止判定点”：
   - 窗口关闭之前按过键 → 缓存，窗口关闭时切段；
   - 窗口关闭之后按的键 → 直接丢弃，不误接下一段。
```

一句话总结：**把“什么时候能按”变成“什么时候之前按都行”。**

---

## 2. 改动前后对比

| 项目 | 现在的代码 | 方案 B 之后 |
|---|---|---|
| `WaitInputPress` 创建时机 | `ComboWindow` 打开时 | 每一段攻击开始时 |
| `WaitInputPress` 销毁时机 | 窗口关闭时（且按下后会残留指针） | 切段或 Ability 结束时 |
| 缓存按键的时间范围 | 只有窗口内 | 本段开始 → 窗口关闭 |
| 窗口关闭后的按键 | 丢弃 | 丢弃（用 `bComboWindowPassed` 锁住） |
| 动画/资源 | 不需要改 | 不需要改 |

本次只改一个文件：

```text
D:\UE2026\First\Source\First\Public\AbilitySystem\Abilities\DK\FirstGA_DKLightAttack.h
D:\UE2026\First\Source\First\Private\AbilitySystem\Abilities\DK\FirstGA_DKLightAttack.cpp
```

---

## 3. 先理解几个核心概念

### 3.1 UAbilityTask_WaitInputPress

这是 GAS 提供的一个 Ability Task，职责是：

```text
在 Ability 存活期间，监听“下一次按下输入”事件。
每按一次，就会调用一次绑定的回调。
```

它有两个特点，和方案 B 直接相关：

- **创建后不会自动结束**：它会一直监听，直到你主动 `EndTask()`，或所属 Ability 结束。
- `WaitInputPress(this, false)` 的第二个参数 `bTestAlreadyPressed` 传 `false`，表示创建时**不检查**按键是否已经按住，只等待“新的按下”事件。

### 3.2 bWantsNextCombo 是什么

它是你的“连招意图缓存位”：

```text
true  = 玩家已经在窗口关闭前按过攻击键，转场时应该进入下一段。
false = 玩家没有按，或按键已经消耗掉了。
```

它不是“立即切段”的开关，只是一个**待办标记**；真正切段的时机仍然是 `ComboWindow` 关闭。

### 3.3 为什么需要 bComboWindowPassed（新标志）

窗口关闭前和关闭后，按键应该有不同的结果：

```text
窗口关闭前按键 → 缓存（可能将来切段）
窗口关闭后按键 → 丢弃
```

当输入任务从“段开始”就存在时，任务本身无法区分“现在是窗口前还是窗口后”，所以需要一个 bool 记录“窗口是否已经过去”：

```text
bComboWindowPassed = false → 还可以缓存输入
bComboWindowPassed = true  → 已经过了转场点，输入直接忽略
```

---

## 4. 修改步骤

### 4.1 第一步：头文件新增一个状态变量

文件：`Source/First/Public/AbilitySystem/Abilities/DK/FirstGA_DKLightAttack.h`

找到：

```cpp
	// 当前段使用 1-based 编号，便于直接写入 ComboCount SetByCaller。
	int32 CurrentComboStep = 0;
	bool bComboWindowOpen = false;
	bool bWantsNextCombo = false;
```

在 `bWantsNextCombo` 下方新增：

```cpp
	// 本段的 ComboWindow 是否已经关闭。
	// false：窗口还没结束，收到的攻击输入会被缓存；
	// true：窗口已经结束，之后的攻击输入直接丢弃。
	bool bComboWindowPassed = false;
```

### 4.2 第二步：ActivateAbility 初始化状态

文件：`Source/First/Private/AbilitySystem/Abilities/DK/FirstGA_DKLightAttack.cpp`

找到 `ActivateAbility()` 里的初始化块：

```cpp
	CurrentComboStep = 1;
	bComboWindowOpen = false;
	bWantsNextCombo = false;
	CurrentAttackMontage = nullptr;
	bTransitionToNextComboStep = false;
```

在 `bWantsNextCombo = false;` 下方新增一行：

```cpp
	bComboWindowPassed = false;
```

### 4.3 第三步：StartCurrentComboStep 重置状态并创建持久输入任务

找到 `StartCurrentComboStep()` 中的这段：

```cpp
	// Attack_1 → Attack_2 时，不允许上一段的取消窗口泄漏到下一段前摇。
	ClearActionCancelTags();

	bComboWindowOpen = false;
	bWantsNextCombo = false;
	if (ComboInputTask)
	{
		ComboInputTask->EndTask();
		ComboInputTask = nullptr;
	}
```

改为：

```cpp
	// Attack_1 → Attack_2 时，不允许上一段的取消窗口泄漏到下一段前摇。
	ClearActionCancelTags();

	bComboWindowOpen = false;
	// 新一段攻击开始时，窗口还没有过去，允许缓存输入。
	bComboWindowPassed = false;
	bWantsNextCombo = false;

	// 结束上一段遗留的输入任务：任务有明确生命周期，避免多个任务累积。
	if (ComboInputTask)
	{
		ComboInputTask->EndTask();
		ComboInputTask = nullptr;
	}
```

然后在 `StartCurrentComboStep()` 的末尾、`MontageTask->ReadyForActivation();` 之后新增：

```cpp
	// 方案 B：本段一开始就创建输入监听，而不是等 ComboWindow 打开。
	// 这样起手阶段按下的攻击键也会被缓存，不再需要精确卡窗口。
	ComboInputTask = UAbilityTask_WaitInputPress::WaitInputPress(this, false);
	ComboInputTask->OnPress.AddDynamic(this, &ThisClass::HandleComboInputPressed);
	ComboInputTask->ReadyForActivation();
```

说明：

- 这个任务会一直存活到本段结束（切段时在函数开头被清理，Ability 结束时在 `EndAbility` 里被清理）。
- 窗口打开事件以后**不再创建任务**，只是记录状态。

### 4.4 第四步：HandleComboWindowOpened 只记录窗口状态

原代码：

```cpp
// 作用：动画进入可接招区间时，才创建一次 WaitInputPress。
void UFirstGA_DKLightAttack::HandleComboWindowOpened(FGameplayEventData Payload)
{

	UE_LOG(
	LogTemp,
	Warning,
	TEXT("[ComboTrace][GA] Window opened | IsActive=%d | WasOpen=%d"),
	IsActive(),
	bComboWindowOpen
);
	if (!IsActive() || bComboWindowOpen || bWantsNextCombo)
	{
		return;
	}

	bComboWindowOpen = true;
	ComboInputTask = UAbilityTask_WaitInputPress::WaitInputPress(this, false);
	ComboInputTask->OnPress.AddDynamic(this, &ThisClass::HandleComboInputPressed);
	ComboInputTask->ReadyForActivation();
}
```

改为：

```cpp
// 作用：连击窗口打开时，只记录“窗口已打开”的状态。
// 输入监听任务已经在 StartCurrentComboStep 中创建，这里不再重复创建。
void UFirstGA_DKLightAttack::HandleComboWindowOpened(FGameplayEventData Payload)
{
	if (!IsActive())
	{
		return;
	}

	bComboWindowOpen = true;

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ComboTrace][GA] Window opened | IsActive=%d"),
		IsActive()
	);
}
```

### 4.5 第五步：HandleComboInputPressed 改成“段内任意时刻缓存”

原代码：

```cpp
// 作用：只记录玩家在有效窗口内确实再次按下；是否跳到下一段等当前 Montage 完成再决定。
void UFirstGA_DKLightAttack::HandleComboInputPressed(float TimeWaited)
{
	UE_LOG(
	LogTemp,
	Warning,
	TEXT("[ComboTrace][GA] Input received in combo task | WindowOpen=%d | TimeWaited=%.3f"),
	bComboWindowOpen,
	TimeWaited
);
	if (!bComboWindowOpen)
	{
		return;
	}

	bWantsNextCombo = true;
	bComboWindowOpen = false;
	ComboInputTask = nullptr;
}
```

改为：

```cpp
// 作用：本段窗口关闭之前，任何一次攻击输入都会被缓存为“想接下一段”。
// 窗口关闭之后的输入会因 bComboWindowPassed 而被忽略。
void UFirstGA_DKLightAttack::HandleComboInputPressed(float TimeWaited)
{
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ComboTrace][GA] Input received | Passed=%d | WantsNext=%d | TimeWaited=%.3f"),
		bComboWindowPassed,
		bWantsNextCombo,
		TimeWaited
	);

	// 已经缓存过一次，或窗口已经关闭：后续输入直接忽略。
	if (bWantsNextCombo || bComboWindowPassed)
	{
		return;
	}

	bWantsNextCombo = true;

	// 注意：这里不再把 ComboInputTask 置空，也不再结束它。
	// 任务继续存活，用于接收“窗口关闭前”的输入；
	// 清理统一交给 StartCurrentComboStep 和 EndAbility。
}
```

### 4.6 第六步：HandleComboWindowClosed 关闭输入窗口并决定转场

原代码：

```cpp
// 作用：动画离开接招区间时取消尚未触发的输入任务，严格拒绝窗口外输入。
void UFirstGA_DKLightAttack::HandleComboWindowClosed(FGameplayEventData Payload)
{

	UE_LOG(LogTemp,Warning,TEXT("[ComboTrace][GA] Window closed | WantsNext=%d"),bWantsNextCombo);

	bComboWindowOpen = false;
	if (ComboInputTask)
	{
		ComboInputTask->EndTask();
		ComboInputTask = nullptr;
	}

	// 玩家在窗口内按过攻击，且动画正到达“后摇开始前”的窗口结束点。
	// 此时主动结束旧 Montage，跳过后摇。
	if (bWantsNextCombo)
	{
		RequestNextComboStepTransition();
	}
}
```

改为：

```cpp
// 作用：窗口关闭 = 本段的转场决策点。
// 窗口关闭前缓存过输入 → 切下一段；没有 → 正常播完收招。
void UFirstGA_DKLightAttack::HandleComboWindowClosed(FGameplayEventData Payload)
{
	bComboWindowOpen = false;
	// 窗口已经结束：之后的输入不再进入缓存。
	bComboWindowPassed = true;

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ComboTrace][GA] Window closed | WantsNext=%d"),
		bWantsNextCombo
	);

	// 玩家在窗口关闭前按过攻击，且动画正到达“后摇开始前”的窗口结束点。
	// 此时主动结束旧 Montage，跳过后摇。
	if (bWantsNextCombo)
	{
		RequestNextComboStepTransition();
	}
}
```

说明：这里**不再调用 `ComboInputTask->EndTask()`**。任务继续存活，但窗口关闭后它收到的每一次按键都会在 `HandleComboInputPressed` 里被 `bComboWindowPassed` 挡住，效果等同丢弃。统一清理仍然在 `StartCurrentComboStep` 和 `EndAbility` 中执行，任务生命周期更清晰。

### 4.7 第七步：EndAbility 重置新状态

找到 `EndAbility()` 中的重置块：

```cpp
	CurrentComboStep = 0;
	bComboWindowOpen = false;
	bWantsNextCombo = false;
	CurrentAttackMontage = nullptr;
	bTransitionToNextComboStep = false;
```

在 `bWantsNextCombo = false;` 下方新增：

```cpp
	bComboWindowPassed = false;
```

这样无论攻击正常结束、被闪避取消、死亡打断，状态都会清干净。

---

## 5. 每个函数的职责讲解（修改后）

### 5.1 ActivateAbility

职责：验证武器、提交能力、把连招状态复位，然后启动监听并播放第一段。

新增的 `bComboWindowPassed = false;` 保证：即使上一次连招异常结束，新连招也不会继承“窗口已过”的旧状态。

### 5.2 StartCurrentComboStep

职责：准备并播放一段攻击。现在它还承担：

```text
1. 清理上一段的取消权限标签；
2. 复位三个输入状态（bComboWindowOpen / bComboWindowPassed / bWantsNextCombo）；
3. 结束上一段遗留的 ComboInputTask；
4. 播放当前段 Montage；
5. 创建本段的持久输入任务。
```

这里的顺序很重要：**先清状态、再建任务**。如果顺序反了，上一段的按键可能被新任务立刻处理。

### 5.3 HandleComboWindowOpened

职责：窗口打开时只记录 `bComboWindowOpen = true`。

它不再创建任务，因为任务在本段开始时已经存在。这个函数现在主要保留“窗口状态”语义，方便日志和以后扩展。

### 5.4 HandleComboInputPressed

职责：**输入缓存的唯一入口**。

```text
bWantsNextCombo || bComboWindowPassed → 忽略
否则 → bWantsNextCombo = true
```

这段代码决定了：

- 玩家在起手阶段按 → 缓存；
- 玩家在窗口内按 → 缓存；
- 玩家已经缓存过一次，再按 → 忽略（防止重复切段）；
- 玩家在窗口关闭后按 → 忽略。

### 5.5 HandleComboWindowClosed

职责：**转场决策点**。

```text
bComboWindowPassed = true
→ bWantsNextCombo 为 true → RequestNextComboStepTransition()
→ 否则什么都不做，让收招自然播完
```

### 5.6 RequestNextComboStepTransition

职责：停止当前蒙太奇，触发“预期的中断”，让 `HandleMontageCancelled` 进入下一段。

这里没有改动，但它是整个缓冲机制的“执行者”：缓存的意义最终在这里兑现。

### 5.7 HandleMontageCancelled

职责：区分两种中断：

```text
bTransitionToNextComboStep == true → 连招转场，播放下一段
否则 → 异常打断（闪避、死亡等），结束整次攻击
```

### 5.8 EndAbility

职责：最终清理。

```text
结束 ComboInputTask
关闭武器碰撞
复位所有状态（包括新的 bComboWindowPassed）
清理取消权限标签
```

---

## 6. 状态变化完整时序

以 Attack_1 → Attack_2 为例：

```text
StartCurrentComboStep（Attack_1）
  bComboWindowOpen   = false
  bComboWindowPassed = false   ← 可以缓存输入
  bWantsNextCombo    = false
  创建 ComboInputTask
  播放 AM_DK_Sword_Attack_1

玩家在起手阶段按攻击键
  HandleComboInputPressed
  bComboWindowPassed == false → bWantsNextCombo = true

ComboWindow 打开（NotifyBegin）
  HandleComboWindowOpened → bComboWindowOpen = true

ComboWindow 关闭（NotifyEnd）
  HandleComboWindowClosed
  bComboWindowPassed = true
  bWantsNextCombo == true → RequestNextComboStepTransition
  → Montage_Stop
  → HandleMontageCancelled（转场分支）
  → StartCurrentComboStep（Attack_2，状态全部复位）
```

如果玩家在窗口关闭前**没有**按过攻击键：

```text
ComboWindow 关闭
  bComboWindowPassed = true
  bWantsNextCombo == false → 不转场
  Attack_1 继续播放收招 → HandleMontageCompleted → FinishAttack(false)
```

---

## 7. 编译与验证

### 7.1 编译

关闭 PIE，编译 `FirstEditor Win64 Development`。

常见编译错误：

| 报错 | 原因 | 修复 |
|---|---|---|
| `bComboWindowPassed 未声明` | 只改了 cpp，没改 h | 确认头文件已添加成员 |
| `HandleComboInputPressed 参数不匹配` | 误改了函数签名 | 保持 `(float TimeWaited)` 不变 |

### 7.2 测试清单

| 测试 | 操作 | 预期结果 |
|---|---|---|
| 起手缓存 | Attack_1 刚起手就按一次攻击键 | 不必卡窗口，Attack_1 结束后自动接 Attack_2 |
| 窗口内按键 | 窗口内按攻击键 | 正常衔接（和以前一致） |
| 窗口后按键 | 窗口关闭、收招阶段按攻击键 | 不衔接，Attack_1 完整播完并结束连击 |
| 狂按 | 连续快速按多次 | 只缓存一次，不会重复切段 |
| 完整连招 | 每段起手各按一次 | Attack_1 → 2 → 3 → 4 正常衔接 |
| 最后一段 | Attack_4 播放期间按键 | 无第五段，Attack_4 完整播完 |
| 闪避取消后 | 攻击被闪避打断，再重新攻击 | 新连招状态干净，没有残留缓存 |

### 7.3 临时调试日志

如果还有问题，在 `HandleComboInputPressed` 的日志中观察：

```text
[ComboTrace][GA] Input received | Passed=0 | WantsNext=0
```

表示按键被正确缓存；

```text
Passed=1
```

表示按键因窗口已关闭被丢弃。

---

## 8. 与方案 A、方案 C 的关系

### 8.1 与方案 A（拉宽 Begin）

做完方案 B 后，`ComboWindow` 的 Begin 位置**不再影响输入接受范围**——输入从段开始就在监听，所以即使不拉宽 Begin，起手按键也能缓存。

如果你之前已经按方案 A 拉宽了 Begin，也不会有冲突，可以保留。

### 8.2 与方案 C（关闭后宽限）

方案 C 是在窗口关闭后再等 0.1 秒的宽限，用来容忍“按晚了一点”。

方案 B 解决的是“按早了”；
方案 C 解决的是“按晚了”。

两个可以同时用：B 负责整段缓冲，C 负责关闭后的短暂宽限。建议先单独验证 B，手感还不够再叠加 C。

---

## 9. 最终逻辑一句话

```text
输入监听覆盖整个攻击段；
bWantsNextCombo 负责缓存意图；
bComboWindowPassed 负责锁定截止点；
ComboWindow 关闭负责决定是否执行转场。
```
