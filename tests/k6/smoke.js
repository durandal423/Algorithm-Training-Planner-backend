import http from 'k6/http';
import { check, sleep } from 'k6';

export const options = {
  vus: 1,
  iterations: 1,
  thresholds: {
    http_req_failed: ['rate<0.05'],
  },
};

const BASE_URL = __ENV.BASE_URL || 'http://localhost:8080';
const JSON_HEADERS = { 'Content-Type': 'application/json' };
http.setResponseCallback(http.expectedStatuses({ min: 200, max: 399 }, 400));

export default function () {
  const preflight = http.request('OPTIONS', `${BASE_URL}/api/problems`, null, {
    headers: { Origin: 'http://localhost:5173', 'Access-Control-Request-Method': 'POST' },
  });
  check(preflight, {
    'cors preflight ok': (r) => r.status === 204,
  });

  const health = http.get(`${BASE_URL}/health`);
  check(health, {
    'health ok': (r) => r.status === 200 && r.json('status') === 'ok',
  });

  const problems = http.get(`${BASE_URL}/api/problems?page=1&page_size=5`);
  check(problems, {
    'problems listed': (r) => r.status === 200 && r.json('count') >= 1,
    'problems pagination metadata present': (r) => r.status === 200 && r.json('total_count') >= r.json('count') && r.json('pagination.page') === 1,
  });

  const tags = http.get(`${BASE_URL}/api/tags`);
  check(tags, {
    'tags listed': (r) => r.status === 200 && r.json('count') >= 1,
  });

  const goalPayload = JSON.stringify({
    name: `k6 smoke ${Date.now()}`,
    description: 'black-box smoke test goal',
    target_count: 3,
    time_budget_minutes: 90,
    difficulty_min: 900,
    difficulty_max: 1800,
    target_tags: ['动态规划', '双指针', '图论'],
    prefer_wrong_problems: true,
    prefer_weak_tags: true,
  });
  const goal = http.post(`${BASE_URL}/api/training-goals`, goalPayload, { headers: JSON_HEADERS });
  check(goal, {
    'goal created': (r) => r.status === 201 && r.json('id') > 0,
  });
  const goalId = goal.json('id');

  const generated = http.post(
    `${BASE_URL}/api/training-plans/generate`,
    JSON.stringify({ goal_id: goalId }),
    { headers: JSON_HEADERS },
  );
  check(generated, {
    'plan generated': (r) => r.status === 201 && r.json('plan_id') > 0,
    'plan has items': (r) => r.json('items').length > 0,
  });

  const planId = generated.json('plan_id');
  const firstProblemId = generated.json('items.0.id');
  const record = http.post(
    `${BASE_URL}/api/training-records`,
    JSON.stringify({
      plan_id: planId,
      problem_id: firstProblemId,
      is_finished: true,
      is_first_try_ac: false,
      actual_minutes: 28,
      error_type: '代码实现错误',
      review_note: 'k6 smoke review note',
    }),
    { headers: JSON_HEADERS },
  );
  check(record, {
    'record created': (r) => r.status === 201 && r.json('id') > 0,
  });

  const practiced = http.get(`${BASE_URL}/api/problems?last_practiced_from=2000-01-01&page_size=5`);
  check(practiced, {
    'recent practice filter ok': (r) => r.status === 200 && r.json('count') >= 1,
  });

  const status = http.put(
    `${BASE_URL}/api/training-plans/${planId}/status`,
    JSON.stringify({ status: 'completed' }),
    { headers: JSON_HEADERS },
  );
  check(status, {
    'plan status updated': (r) => r.status === 200 && r.json('status') === 'completed',
  });

  const invalidStatus = http.put(
    `${BASE_URL}/api/training-plans/${planId}/status`,
    JSON.stringify({ status: 'bogus' }),
    { headers: JSON_HEADERS },
  );
  check(invalidStatus, {
    'invalid status rejected': (r) => r.status === 400 && typeof r.json('error') === 'string',
  });

  const summary = http.get(`${BASE_URL}/api/dashboard/summary`);
  check(summary, {
    'dashboard summary ok': (r) => r.status === 200 && r.json('total_problems') >= 1,
  });

  const deletePlan = http.del(`${BASE_URL}/api/training-plans/${planId}`);
  check(deletePlan, {
    'plan deleted': (r) => r.status === 200 && r.json('deleted') === true,
  });

  const deleteGoal = http.del(`${BASE_URL}/api/training-goals/${goalId}`);
  check(deleteGoal, {
    'goal deleted': (r) => r.status === 200 && r.json('deleted') === true,
  });

  sleep(1);
}
