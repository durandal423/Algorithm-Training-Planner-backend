INSERT INTO users(username, display_name)
VALUES ('default', '默认训练者')
ON CONFLICT(username) DO NOTHING;

INSERT INTO tags(name, description, mastery_score, wrong_count) VALUES
('双指针', '使用左右指针维护区间或配对关系。', 58, 2),
('滑动窗口', '连续区间维护与窗口收缩。', 55, 3),
('动态规划', '状态设计、转移与优化。', 46, 5),
('贪心', '局部最优策略与证明。', 64, 1),
('图论', '最短路、连通性、拓扑等图算法。', 48, 4),
('搜索', 'DFS、BFS、回溯与剪枝。', 62, 1),
('数论', '质数、同余、组合计数。', 57, 2),
('数据结构', '堆、并查集、树状数组、线段树。', 52, 3),
('状态压缩', '使用位集合压缩状态空间。', 44, 5),
('排序', '排序与有序性维护。', 70, 0),
('哈希表', '哈希映射与集合统计。', 66, 1),
('连通性', '连通分量与可达性建模。', 47, 4),
('前缀和', '区间和与差分统计。', 61, 1),
('二分', '单调性判断与答案搜索。', 59, 2)
ON CONFLICT(name) DO UPDATE SET
    description = EXCLUDED.description,
    mastery_score = EXCLUDED.mastery_score,
    wrong_count = EXCLUDED.wrong_count;

INSERT INTO problems(problem_code, title, source_platform, source_url, difficulty, estimated_minutes, summary, is_completed, is_wrong_problem, wrong_count) VALUES
('P12134', '画展布置', '蓝桥杯', 'https://www.luogu.com.cn/problem/P12134', 1300, 25, '从画作区间中选择稳定难度窗口，对应双指针与滑动窗口能力迁移。', FALSE, TRUE, 2),
('P12135', '水质检测', '蓝桥杯', 'https://www.luogu.com.cn/problem/P12135', 1700, 40, '状态压缩动态规划处理连通性检测，对应水质检测竞赛题迁移。', FALSE, TRUE, 3),
('T0001', '区间最大连续和', '洛谷', 'https://example.com/problems/T0001', 1200, 25, '经典子段和状态转移训练。', TRUE, FALSE, 0),
('T0002', '数组去重统计', 'LeetCode', 'https://example.com/problems/T0002', 900, 15, '哈希集合与去重计数。', TRUE, FALSE, 0),
('T0003', '最短路径模板', 'Codeforces', 'https://example.com/problems/T0003', 1500, 35, 'Dijkstra 模板与边权建模。', FALSE, TRUE, 1),
('T0004', '有序数组配对', '洛谷', 'https://example.com/problems/T0004', 1100, 20, '双指针完成两数配对。', TRUE, FALSE, 0),
('T0005', '背包训练计划', '蓝桥杯', 'https://example.com/problems/T0005', 1600, 35, '01 背包与选择恢复。', FALSE, TRUE, 2),
('T0006', '会议活动安排', '洛谷', 'https://example.com/problems/T0006', 1000, 18, '区间贪心选择。', TRUE, FALSE, 0),
('T0007', '岛屿数量统计', 'LeetCode', 'https://example.com/problems/T0007', 1200, 25, '网格 BFS 与连通块统计。', FALSE, FALSE, 0),
('T0008', '质数筛选器', '蓝桥杯', 'https://example.com/problems/T0008', 1000, 20, '埃氏筛与复杂度分析。', TRUE, FALSE, 0),
('T0009', '状态压缩旅行', 'Codeforces', 'https://example.com/problems/T0009', 1900, 50, '位压缩 TSP 入门。', FALSE, TRUE, 3),
('T0010', '窗口内最大值', '洛谷', 'https://example.com/problems/T0010', 1400, 30, '滑动窗口和单调队列思想。', FALSE, TRUE, 1),
('T0011', '课程学习顺序', 'LeetCode', 'https://example.com/problems/T0011', 1450, 30, '拓扑排序与环检测。', FALSE, FALSE, 0),
('T0012', '树状数组求和', '洛谷', 'https://example.com/problems/T0012', 1350, 30, '动态前缀和维护。', TRUE, FALSE, 0),
('T0013', '最长上升子序列', '蓝桥杯', 'https://example.com/problems/T0013', 1500, 30, 'LIS 动态规划与二分优化。', FALSE, TRUE, 1),
('T0014', '二分答案切木头', '洛谷', 'https://example.com/problems/T0014', 1150, 20, '答案单调性与二分判断。', TRUE, FALSE, 0),
('T0015', '并查集朋友圈', 'LeetCode', 'https://example.com/problems/T0015', 1250, 25, '并查集维护连通性。', FALSE, FALSE, 0),
('T0016', '前缀和矩阵', '蓝桥杯', 'https://example.com/problems/T0016', 1100, 22, '二维前缀和快速查询。', TRUE, FALSE, 0),
('T0017', '最小覆盖子串', 'LeetCode', 'https://example.com/problems/T0017', 1550, 35, '滑动窗口维护字符需求。', FALSE, TRUE, 2),
('T0018', '数位拆分计数', 'Codeforces', 'https://example.com/problems/T0018', 1650, 35, '计数 DP 与取模。', FALSE, FALSE, 0),
('T0019', '图的连通块染色', '洛谷', 'https://example.com/problems/T0019', 1300, 28, 'DFS 染色判断连通分量。', TRUE, FALSE, 0),
('T0020', '括号生成搜索', 'LeetCode', 'https://example.com/problems/T0020', 1050, 18, '回溯生成合法序列。', TRUE, FALSE, 0),
('T0021', '股票买卖冷冻期', 'LeetCode', 'https://example.com/problems/T0021', 1550, 32, '多状态动态规划。', FALSE, TRUE, 1),
('T0022', '合并区间复盘', '洛谷', 'https://example.com/problems/T0022', 950, 18, '排序后合并重叠区间。', TRUE, FALSE, 0),
('T0023', '双端队列选择', 'Codeforces', 'https://example.com/problems/T0023', 1450, 32, '双指针与贪心判断。', FALSE, FALSE, 0),
('T0024', '网格最短路', '蓝桥杯', 'https://example.com/problems/T0024', 1400, 30, 'BFS 最短路与障碍建模。', FALSE, TRUE, 1),
('T0025', '子集覆盖练习', '洛谷', 'https://example.com/problems/T0025', 1750, 42, '状态压缩表示标签覆盖。', FALSE, TRUE, 2),
('T0026', '逆序对统计', '洛谷', 'https://example.com/problems/T0026', 1500, 32, '归并排序与树状数组。', FALSE, FALSE, 0),
('T0027', '最大不相交线段', '蓝桥杯', 'https://example.com/problems/T0027', 1350, 26, '排序加贪心选择。', TRUE, FALSE, 0),
('T0028', '单词接龙图', 'LeetCode', 'https://example.com/problems/T0028', 1800, 45, '图建模与 BFS 搜索。', FALSE, TRUE, 2),
('T0029', '模运算路径数', 'Codeforces', 'https://example.com/problems/T0029', 1650, 36, '数论结合 DP 计数。', FALSE, FALSE, 0),
('T0030', '最长无重复子数组', 'LeetCode', 'https://example.com/problems/T0030', 1250, 24, '哈希表与滑动窗口维护。', TRUE, FALSE, 0),
('T0031', '线段树区间修改', '洛谷', 'https://example.com/problems/T0031', 1850, 48, '线段树懒标记训练。', FALSE, TRUE, 2),
('T0032', '可达状态压缩', '蓝桥杯', 'https://example.com/problems/T0032', 1750, 42, '用 bitmask 压缩访问状态。', FALSE, TRUE, 2)
ON CONFLICT(problem_code) DO NOTHING;

INSERT INTO problem_tags(problem_id, tag_id)
SELECT p.id, t.id
FROM (VALUES
('P12134', '排序'), ('P12134', '双指针'), ('P12134', '滑动窗口'), ('P12134', '贪心'),
('P12135', '动态规划'), ('P12135', '状态压缩'), ('P12135', '连通性'),
('T0001', '动态规划'), ('T0001', '前缀和'),
('T0002', '哈希表'), ('T0002', '数据结构'),
('T0003', '图论'), ('T0003', '数据结构'),
('T0004', '双指针'), ('T0004', '排序'),
('T0005', '动态规划'), ('T0005', '数据结构'),
('T0006', '贪心'), ('T0006', '排序'),
('T0007', '搜索'), ('T0007', '连通性'),
('T0008', '数论'),
('T0009', '动态规划'), ('T0009', '状态压缩'),
('T0010', '滑动窗口'), ('T0010', '数据结构'),
('T0011', '图论'), ('T0011', '搜索'),
('T0012', '数据结构'), ('T0012', '前缀和'),
('T0013', '动态规划'), ('T0013', '二分'),
('T0014', '二分'), ('T0014', '贪心'),
('T0015', '数据结构'), ('T0015', '连通性'),
('T0016', '前缀和'),
('T0017', '滑动窗口'), ('T0017', '双指针'), ('T0017', '哈希表'),
('T0018', '动态规划'), ('T0018', '数论'),
('T0019', '图论'), ('T0019', '搜索'), ('T0019', '连通性'),
('T0020', '搜索'),
('T0021', '动态规划'),
('T0022', '排序'), ('T0022', '贪心'),
('T0023', '双指针'), ('T0023', '贪心'),
('T0024', '图论'), ('T0024', '搜索'),
('T0025', '状态压缩'), ('T0025', '动态规划'),
('T0026', '排序'), ('T0026', '数据结构'),
('T0027', '排序'), ('T0027', '贪心'),
('T0028', '图论'), ('T0028', '搜索'),
('T0029', '数论'), ('T0029', '动态规划'),
('T0030', '哈希表'), ('T0030', '滑动窗口'), ('T0030', '双指针'),
('T0031', '数据结构'),
('T0032', '状态压缩'), ('T0032', '搜索')
) AS mapping(problem_code, tag_name)
JOIN problems p ON p.problem_code = mapping.problem_code
JOIN tags t ON t.name = mapping.tag_name
ON CONFLICT DO NOTHING;

INSERT INTO training_goals(
    user_id, name, description, target_count, time_budget_minutes,
    difficulty_min, difficulty_max, prefer_wrong_problems, prefer_weak_tags
)
SELECT u.id, '蓝桥杯算法复盘计划', '覆盖动态规划、双指针和图论的示例训练目标。', 8, 180,
       1000, 1800, TRUE, TRUE
FROM users u
WHERE u.username = 'default'
  AND NOT EXISTS (SELECT 1 FROM training_goals WHERE name = '蓝桥杯算法复盘计划');

INSERT INTO training_goal_tags(goal_id, tag_id)
SELECT g.id, t.id
FROM training_goals g
JOIN tags t ON t.name IN ('动态规划', '双指针', '图论')
WHERE g.name = '蓝桥杯算法复盘计划'
ON CONFLICT DO NOTHING;
