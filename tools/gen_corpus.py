#!/usr/bin/env python3
"""生成 ESP32-S3-NLU 训练语料（阶段 4）。

从 tools/intents.json 展开「模板 × 槽位」组合，按模板均分采样，模板级
划分 train / val / test（8:1:1，防模板泄漏），并为 unknown 生成噪声样本。
全部产物由「语料源 + 固定种子」确定性复现（TSV / vocab 逐字节一致），
不入库；结构说明见 docs/STAGE4_DESIGN.md。

用法（在工程根目录执行）：
  python tools/gen_corpus.py                # 种子取语料源 meta.seed（42）
  python tools/gen_corpus.py --seed 7       # 覆盖种子（采样与划分都会变）
  python tools/gen_corpus.py --out D:/tmp/corpus

产物（默认 data/corpus/，不入库）：
  train.tsv / val.tsv / test.tsv   制表符分隔：句子 + 意图标签
  vocab.txt                        行号即 id：<pad>=0、<unk>=1，其后按频次降序
  report.md                        统计与自检报告（含生成时间，不作一致性比对对象）
"""
import argparse
import json
import random
import re
from collections import Counter
from datetime import datetime
from itertools import product
from pathlib import Path

SLOT_RE = re.compile(r"\{(\w+)\}")

MAX_TPL_LEN = 20        # 模板句字符数上限（设计约定，见 STAGE4_DESIGN）
MAX_NOISE_LEN = 22      # 超长噪声拼接后的截断长度（避免抬高 max_len）
VOCAB_LIMIT = 500       # 词表总行数上限（含 <pad> / <unk>）

# 部署期探针：这些说法的字符必须全在词表内（阶段 1 验收句 + 常用命令）
PROBE_SENTENCES = [
    "开灯", "关灯", "把灯打开", "闪起来", "让灯闪一下", "报警",
    "呼吸", "停止", "安静", "别响了", "把蜂鸣器关掉", "全停",
]

# 噪声字符池：只收真实场景里会被敲进串口的乱输入用字，避免撑大词表
NOISE_SINGLE = ["啊", "哦", "嗯", "的", "了", "吗", "呀", "1", "3", "7", "a", "x", "？", "！"]
NOISE_REPEAT = ["哈", "啊", "嘿", "不", "呀", "a", "6", "哇"]
NOISE_KEYS = "qwertyuiopasdfghjklzxcvbnm"


def expand_by_template(templates, slots):
    """展开模板的槽位组合，返回与模板一一对应的句子分组（组内去重）。"""
    groups = []
    for tpl in templates:
        names = SLOT_RE.findall(tpl)
        if not names:
            groups.append([tpl])
            continue
        seen = set()
        items = []
        for combo in product(*(slots[n] for n in names)):
            s = tpl
            for n, v in zip(names, combo):
                s = s.replace("{" + n + "}", v, 1)
            if s not in seen:
                seen.add(s)
                items.append(s)
        groups.append(items)
    return groups


def sample_by_template(groups, target, rng):
    """按模板均分配额采样：不足配额的模板全取，剩余名额循环均分给有余量的模板。

    返回 (picked, shortfall)；picked 为 [(模板索引, 句子)]，shortfall 为组合
    总量不足时的缺口（模板需要补充的信号）。
    """
    quota = [0] * len(groups)
    cap = [len(g) for g in groups]
    need = target
    active = [i for i, c in enumerate(cap) if c > 0]
    while need > 0 and active:
        share, rem = divmod(need, len(active))
        given = 0
        nxt = []
        for pos, i in enumerate(active):
            want = share + (1 if pos < rem else 0)
            take = min(want, cap[i] - quota[i])
            quota[i] += take
            given += take
            if cap[i] - quota[i] > 0:
                nxt.append(i)
        need -= given
        if given == 0:
            break
        active = nxt
    picked = []
    for i, g in enumerate(groups):
        for s in rng.sample(g, quota[i]):
            picked.append((i, s))
    return picked, target - sum(quota)


def split_templates(template_counts, rng):
    """模板随机分堆，使各集合句数接近 8:1:1。

    各模板产出的句数不等（固定句 1 条、高组合模板十余条），所以按「产出量」
    而非「模板个数」切分：洗牌后逐个把模板放入当前填充率最低的集合。
    参数 template_counts: {模板索引: 句数}；返回 集合名 -> 模板索引集合。
    """
    total = sum(template_counts.values())
    tgt = {"train": max(total * 0.8, 1), "val": max(total * 0.1, 1), "test": max(total * 0.1, 1)}
    idx = sorted(template_counts)
    rng.shuffle(idx)
    buckets = {"train": [], "val": [], "test": []}
    filled = {"train": 0.0, "val": 0.0, "test": 0.0}
    for i in idx:
        name = min(buckets, key=lambda k: filled[k] / tgt[k])
        buckets[name].append(i)
        filled[name] += template_counts[i]
    return {k: set(v) for k, v in buckets.items()}


def gen_noise(n, rng, sentence_pool):
    """按固定比例生成噪声：单字 / 重复 / 乱敲 / 超长拼接。"""
    n_single = round(n * 0.35)
    n_repeat = round(n * 0.25)
    n_keys = round(n * 0.2)
    n_long = n - n_single - n_repeat - n_keys
    items = []
    for _ in range(n_single):
        items.append("".join(rng.sample(NOISE_SINGLE, rng.randint(1, 2))))
    for _ in range(n_repeat):
        items.append(rng.choice(NOISE_REPEAT) * rng.randint(3, 8))
    for _ in range(n_keys):
        items.append("".join(rng.choice(NOISE_KEYS) for _ in range(rng.randint(4, 10))))
    for _ in range(n_long):
        s = "".join(rng.choice(sentence_pool) for _ in range(rng.randint(2, 3)))
        items.append(s[:MAX_NOISE_LEN])
    return items


def intent_length_stats(sentences):
    """字符长度统计：返回 (最短, 中位, P95, 最长)。"""
    lens = sorted(len(s) for s in sentences)
    if not lens:
        return 0, 0, 0, 0
    return lens[0], lens[len(lens) // 2], lens[min(len(lens) - 1, int(len(lens) * 0.95))], lens[-1]


def build_report(seed, args, data, overview, vocab, warnings, noise_n):
    """组装 report.md 内容。"""
    lines = []
    lines.append("# 语料生成报告")
    lines.append("")
    lines.append(f"- 生成时间：{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    try:  # 报告展示相对路径，避免把本机绝对路径写进产物
        src_disp = Path(args.src).relative_to(Path.cwd())
    except ValueError:
        src_disp = Path(args.src).name
    lines.append(f"- 语料源：`{src_disp}`")
    lines.append(f"- 种子：{seed} ｜ 每类目标：{overview[0]['target'] if overview else '-'} ｜ 噪声：{noise_n} 条")
    lines.append("- 产物：`train.tsv` / `val.tsv` / `test.tsv` / `vocab.txt`")
    lines.append("")

    lines.append("## 最终条数")
    lines.append("")
    lines.append("| 意图 | train | val | test | 合计 |")
    lines.append("| --- | --- | --- | --- | --- |")
    total = {"train": 0, "val": 0, "test": 0}
    for intent in data["train"]:
        row = []
        s = 0
        for name in ("train", "val", "test"):
            n = len(data[name].get(intent, []))
            row.append(n)
            s += n
            total[name] += n
        lines.append(f"| {intent} | {row[0]} | {row[1]} | {row[2]} | {s} |")
    lines.append(f"| **合计** | {total['train']} | {total['val']} | {total['test']} | "
                 f"{total['train'] + total['val'] + total['test']} |")
    lines.append("")

    lines.append("## 生成诊断（采样）")
    lines.append("")
    lines.append("| 意图 | 模板数 | 组合数 | 目标 | 采样产出 | 缺口 |")
    lines.append("| --- | --- | --- | --- | --- | --- |")
    for o in overview:
        lines.append(f"| {o['intent']} | {o['templates']} | {o['combos']} | {o['target']} | "
                     f"{o['produced']} | {o['shortfall']} |")
    lines.append("")

    lines.append("## 长度分布（字符数）")
    lines.append("")
    lines.append("| 意图 | 最短 | 中位 | P95 | 最长 |")
    lines.append("| --- | --- | --- | --- | --- |")
    all_lens = []
    for intent in data["train"]:
        sents = [s for name in ("train", "val", "test") for s in data[name].get(intent, [])]
        all_lens.extend(len(s) for s in sents)
        a, b, c, d = intent_length_stats(sents)
        lines.append(f"| {intent} | {a} | {b} | {c} | {d} |")
    all_lens.sort()
    p999 = all_lens[min(len(all_lens) - 1, int(len(all_lens) * 0.999))]
    lines.append("")
    lines.append(f"全局：P99.9 = {p999}，最长 = {all_lens[-1]}，**建议 max_len = {p999}**")
    lines.append("")

    lines.append("## 词表")
    lines.append("")
    chars = vocab[2:]
    top20 = " ".join(c if c.strip() else repr(c) for c in chars[:20])
    lines.append(f"- 总行数：{len(vocab)}（`<pad>`=0、`<unk>`=1，字符 {len(chars)} 个）")
    lines.append(f"- 上限 {VOCAB_LIMIT}：{'通过' if len(vocab) <= VOCAB_LIMIT else '**超限**'}")
    lines.append(f"- Top 20 高频字符：{top20}")
    lines.append("")

    lines.append("## 自检")
    lines.append("")
    lines.append("- 探针句覆盖：通过（阶段 1 验收句字符全部在词表内）")
    lines.append("- 标注冲突 / 跨集合重复：无")
    lines.append("- 模板泄漏：通过（同模板句子只进一个集合，构造保证 + 显式验证）")
    lines.append("")
    if warnings:
        lines.append("## 警告")
        lines.append("")
        for w in warnings:
            lines.append(f"- {w}")
        lines.append("")
    return "\n".join(lines)


def main():
    root = Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser(description="生成 NLU 训练语料（阶段 4）")
    ap.add_argument("--src", default=str(root / "tools" / "intents.json"), help="语料源 JSON 路径")
    ap.add_argument("--out", default=str(root / "data" / "corpus"), help="产物目录")
    ap.add_argument("--seed", type=int, default=None, help="随机种子（默认取语料源 meta.seed）")
    args = ap.parse_args()

    spec = json.loads(Path(args.src).read_text(encoding="utf-8"))
    slots = spec["slots"]
    intents = spec["intents"]
    meta = spec.get("meta", {})
    seed = args.seed if args.seed is not None else meta.get("seed", 42)
    per_intent = meta.get("per_intent", 500)
    unknown_target = meta.get("unknown_target", 600)
    noise_ratio = spec.get("noise", {}).get("ratio", 0.25)

    rng = random.Random(seed)
    errors = []
    warnings = []
    data = {"train": {}, "val": {}, "test": {}}
    overview = []

    # 1) 展开 + 采样 + 模板级划分
    for intent, templates in intents.items():
        groups = expand_by_template(templates, slots)
        for g in groups:
            for s in g:
                if len(s) > MAX_TPL_LEN:
                    errors.append(f"[{intent}] 模板句超长（{len(s)} 字）：{s}")
        combos = sum(len(g) for g in groups)
        target = unknown_target - round(unknown_target * noise_ratio) if intent == "unknown" else per_intent
        picked, shortfall = sample_by_template(groups, target, rng)
        if shortfall > 0:
            warnings.append(f"[{intent}] 组合数不足：目标 {target}，采样产出 {target - shortfall}")
        assign = split_templates(Counter(ti for ti, _ in picked), rng)
        where = {}
        for name, tset in assign.items():
            for ti in sorted(tset):
                if ti in where:
                    errors.append(f"[{intent}] 模板泄漏：模板 {ti} 同时进入多个集合")
                where[ti] = name
        for ti, s in picked:
            data[where[ti]].setdefault(intent, []).append(s)
        overview.append({
            "intent": intent, "templates": len(templates), "combos": combos,
            "target": target, "produced": len(picked), "shortfall": shortfall,
        })

    # 2) unknown 噪声（从已生成句子池拼接长句），8:1:1 直接划入三集
    noise_n = round(unknown_target * noise_ratio)
    pool = [s for name in data for arr in data[name].values() for s in arr]
    # 噪声句的取值空间有限（重复/单字类易撞），多生成 50% 抵去重损耗后截取
    noise = list(dict.fromkeys(gen_noise(round(noise_n * 1.5), rng, pool)))[:noise_n]
    noise_n = len(noise)
    rng.shuffle(noise)
    n_val = max(1, round(noise_n * 0.1))
    n_test = max(1, round(noise_n * 0.1))
    data["val"].setdefault("unknown", []).extend(noise[:n_val])
    data["test"].setdefault("unknown", []).extend(noise[n_val:n_val + n_test])
    data["train"].setdefault("unknown", []).extend(noise[n_val + n_test:])

    # 3) 全局去重 + 标注冲突检查（保留首见）
    seen_label = {}
    for name in ("train", "val", "test"):
        for intent, arr in data[name].items():
            dedup = []
            for s in arr:
                if s in seen_label:
                    if seen_label[s] != intent:
                        errors.append(f"标注冲突：\"{s}\" 同时出现在 {seen_label[s]} 与 {intent}")
                    continue
                seen_label[s] = intent
                dedup.append(s)
            data[name][intent] = dedup

    # 4) 词表 + 探针覆盖
    cnt = Counter()
    for name in data:
        for arr in data[name].values():
            for s in arr:
                cnt.update(s)
    chars = sorted(cnt, key=lambda c: (-cnt[c], ord(c)))
    vocab = ["<pad>", "<unk>"] + chars
    if len(vocab) > VOCAB_LIMIT:
        rare = chars[VOCAB_LIMIT - 2:][:30]
        errors.append(f"词表 {len(vocab)} 行，超过上限 {VOCAB_LIMIT}；"
                      f"最低频字符示例：{''.join(rare)}")
    missing = sorted({ch for p in PROBE_SENTENCES for ch in p if ch not in set(vocab)})
    if missing:
        errors.append(f"探针句字符未覆盖：{''.join(missing)}")

    if errors:
        print("自检未通过，未写产物：")
        for e in errors:
            print("  -", e)
        return 1

    # 5) 写产物
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    totals = {}
    for name in ("train", "val", "test"):
        rows = [(s, intent) for intent, arr in data[name].items() for s in arr]
        rng.shuffle(rows)
        with open(out_dir / f"{name}.tsv", "w", encoding="utf-8", newline="\n") as f:
            for s, intent in rows:
                f.write(f"{s}\t{intent}\n")
        totals[name] = len(rows)
    with open(out_dir / "vocab.txt", "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(vocab) + "\n")
    report = build_report(seed, args, data, overview, vocab, warnings, noise_n)
    (out_dir / "report.md").write_text(report, encoding="utf-8", newline="\n")

    print(f"生成完成（seed={seed}）")
    print(f"  train {totals['train']} 条 ｜ val {totals['val']} 条 ｜ test {totals['test']} 条")
    print(f"  词表 {len(vocab)} 行（上限 {VOCAB_LIMIT}）")
    for w in warnings:
        print("  警告:", w)
    print(f"  报告: {out_dir / 'report.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
