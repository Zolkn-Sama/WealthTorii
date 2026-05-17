// Thin typed client for the WealthTorii API. Same-origin (served by Drogon),
// JWT kept in localStorage.

export type Money = { minor_units: number; currency: string; display: string };

const TOKEN_KEY = "wt_token";

export const tokenStore = {
  get: () => localStorage.getItem(TOKEN_KEY),
  set: (t: string) => localStorage.setItem(TOKEN_KEY, t),
  clear: () => localStorage.removeItem(TOKEN_KEY),
};

export type ApiResult<T> = { ok: true; data: T } | { ok: false; status: number; error: string };

async function request<T>(
  method: string,
  path: string,
  body?: unknown,
): Promise<ApiResult<T>> {
  const headers: Record<string, string> = {};
  const token = tokenStore.get();
  if (token) headers["Authorization"] = `Bearer ${token}`;
  if (body !== undefined) headers["Content-Type"] = "application/json";
  let resp: Response;
  try {
    resp = await fetch(path, {
      method,
      headers,
      body: body !== undefined ? JSON.stringify(body) : undefined,
    });
  } catch (e) {
    return { ok: false, status: 0, error: String(e) };
  }
  let json: unknown = null;
  const text = await resp.text();
  if (text) {
    try {
      json = JSON.parse(text);
    } catch {
      json = null;
    }
  }
  if (!resp.ok) {
    const err =
      (json as { error?: string } | null)?.error ?? `HTTP ${resp.status}`;
    return { ok: false, status: resp.status, error: err };
  }
  return { ok: true, data: json as T };
}

export type Session = {
  token: string;
  token_type: string;
  user: { id: string; email: string; plan: string };
};

export const api = {
  register: (email: string, password: string) =>
    request<Session>("POST", "/api/auth/register", { email, password }),
  login: (email: string, password: string) =>
    request<Session>("POST", "/api/auth/login", { email, password }),
  me: () => request<{ id: string; email: string; plan: string }>("GET", "/api/auth/me"),
  networth: () =>
    request<{
      accounts: Array<{ id: string; name: string; currency: string; type: string; balance: Money; opening_balance: Money }>;
      totals: Array<{
        currency: string;
        net_worth: Money;
        opening_balance: Money;
        investments: Money;
        total: Money;
      }>;
    }>("GET", "/api/networth"),
  goals: () =>
    request<{
      goals: Array<{
        id: string;
        name: string;
        target: Money;
        saved: Money;
        remaining: Money;
        progress_pct: number;
        reached: boolean;
        target_date: string | null;
        required_monthly?: Money;
      }>;
    }>("GET", "/api/goals"),
  trends: (account: string, months = 6) =>
    request<{
      account: string;
      months: Array<{ month: string; inflow: Money; outflow: Money; net: Money; savings_rate_pct: number }>;
    }>("GET", `/api/trends?account=${encodeURIComponent(account)}&months=${months}`),
  forecast: (account: string) =>
    request<{
      account: string;
      as_of: string;
      horizon: string;
      current_balance: Money;
      projected_balance: Money;
      expected: Array<{ date: string; label: string; amount: Money }>;
    }>("GET", `/api/forecast?account=${encodeURIComponent(account)}`),

  // ---- writes ----
  createAccount: (b: {
    id: string;
    name: string;
    currency?: string;
    type?: string;
    opening_balance?: string;
  }) => request<unknown>("POST", "/api/accounts", b),

  createTransaction: (b: {
    id: string;
    account_id: string;
    date: string;
    amount: string;
    description?: string;
    category_id?: string;
  }) => request<unknown>("POST", "/api/transactions", b),

  getBudget: () =>
    request<{
      currency: string;
      total: Money;
      limits: Array<{ category: string; limit: Money }>;
    }>("GET", "/api/budget"),
  setBudget: (b: { category: string; amount: string; currency?: string }) =>
    request<unknown>("POST", "/api/budget", b),

  createGoal: (b: {
    name: string;
    target: string;
    currency?: string;
    target_date?: string;
  }) => request<unknown>("POST", "/api/goals", b),

  addContribution: (
    goalId: string,
    b: { amount: string; date?: string; note?: string },
  ) =>
    request<unknown>(
      "POST",
      `/api/goals/${encodeURIComponent(goalId)}/contributions`,
      b,
    ),

  // ---- updates / deletes ----
  updateAccount: (
    id: string,
    b: { name: string; currency: string; type: string; opening_balance?: string },
  ) =>
    request<unknown>("PUT", `/api/accounts/${encodeURIComponent(id)}`, b),
  deleteAccount: (id: string) =>
    request<unknown>("DELETE", `/api/accounts/${encodeURIComponent(id)}`),

  deleteBudget: (category: string) =>
    request<unknown>(
      "DELETE",
      `/api/budget/${encodeURIComponent(category)}`,
    ),

  updateGoal: (
    id: string,
    b: { name: string; target: string; currency?: string; target_date?: string },
  ) => request<unknown>("PUT", `/api/goals/${encodeURIComponent(id)}`, b),
  deleteGoal: (id: string) =>
    request<unknown>("DELETE", `/api/goals/${encodeURIComponent(id)}`),

  listTransactions: (account: string) =>
    request<{
      account: string;
      count: number;
      transactions: Array<{
        id: string;
        account_id: string;
        date: string;
        amount: Money;
        description: string;
        category_id: string | null;
      }>;
    }>(
      "GET",
      `/api/transactions?account=${encodeURIComponent(account)}`,
    ),
  updateTransaction: (
    id: string,
    b: {
      account_id: string;
      date: string;
      amount: string;
      description?: string;
      category_id?: string;
    },
  ) =>
    request<unknown>("PUT", `/api/transactions/${encodeURIComponent(id)}`, b),
  deleteTransaction: (id: string) =>
    request<unknown>(
      "DELETE",
      `/api/transactions/${encodeURIComponent(id)}`,
    ),

  // ---- investments ----
  portfolio: () =>
    request<{
      positions: Array<{
        id: string;
        account_id: string;
        symbol: string;
        quantity: string;
        cost: Money;
        market_value: Money;
        unrealized: Money;
        return_pct: number;
        priced: boolean;
      }>;
      totals: Array<{
        currency: string;
        cost: Money;
        market_value: Money;
        unrealized: Money;
      }>;
    }>("GET", "/api/portfolio"),
  prices: () =>
    request<{
      prices: Array<{ symbol: string; price: Money; as_of: string }>;
    }>("GET", "/api/prices"),
  setPrice: (
    symbol: string,
    b: { price: string; currency?: string; as_of?: string },
  ) =>
    request<unknown>(
      "PUT",
      `/api/prices/${encodeURIComponent(symbol)}`,
      b,
    ),
  deletePrice: (symbol: string) =>
    request<unknown>(
      "DELETE",
      `/api/prices/${encodeURIComponent(symbol)}`,
    ),
  createPosition: (b: {
    symbol: string;
    quantity: string;
    cost: string;
    currency?: string;
    account_id?: string;
  }) => request<unknown>("POST", "/api/positions", b),
  updatePosition: (
    id: string,
    b: {
      symbol: string;
      quantity: string;
      cost: string;
      currency?: string;
      account_id?: string;
    },
  ) => request<unknown>("PUT", `/api/positions/${encodeURIComponent(id)}`, b),
  deletePosition: (id: string) =>
    request<unknown>(
      "DELETE",
      `/api/positions/${encodeURIComponent(id)}`,
    ),
};

