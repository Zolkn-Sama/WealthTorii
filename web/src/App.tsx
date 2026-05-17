import { useCallback, useEffect, useState, type FormEvent } from "react";
import { api, tokenStore, type ApiResult } from "./api";
import { TrendsChart } from "./TrendsChart";

export function App() {
  const [token, setToken] = useState<string | null>(tokenStore.get());
  if (!token) {
    return (
      <AuthView
        onAuthed={(t) => {
          tokenStore.set(t);
          setToken(t);
        }}
      />
    );
  }
  return (
    <Dashboard
      onLogout={() => {
        tokenStore.clear();
        setToken(null);
      }}
    />
  );
}

function AuthView({ onAuthed }: { onAuthed: (token: string) => void }) {
  const [mode, setMode] = useState<"login" | "register">("login");
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");

  async function submit(e: FormEvent) {
    e.preventDefault();
    setBusy(true);
    setError("");
    const r =
      mode === "login"
        ? await api.login(email, password)
        : await api.register(email, password);
    setBusy(false);
    if (r.ok) onAuthed(r.data.token);
    else setError(r.error);
  }

  return (
    <div className="center">
      <form className="card auth" onSubmit={submit}>
        <h1>WealthTorii</h1>
        <p className="muted">
          {mode === "login" ? "Connexion" : "Créer un compte"}
        </p>
        <input
          type="email"
          placeholder="email"
          value={email}
          required
          onChange={(e) => setEmail(e.target.value)}
        />
        <input
          type="password"
          placeholder="mot de passe (≥ 8)"
          value={password}
          required
          minLength={8}
          onChange={(e) => setPassword(e.target.value)}
        />
        {error && <div className="error">{error}</div>}
        <button disabled={busy} type="submit">
          {busy ? "…" : mode === "login" ? "Se connecter" : "S'inscrire"}
        </button>
        <button
          type="button"
          className="link"
          onClick={() => setMode(mode === "login" ? "register" : "login")}
        >
          {mode === "login"
            ? "Pas de compte ? S'inscrire"
            : "Déjà un compte ? Se connecter"}
        </button>
      </form>
    </div>
  );
}

type Field = {
  name: string;
  label: string;
  type?: "text" | "date" | "number";
  required?: boolean;
  options?: Array<{ value: string; label: string }>;
};

// Minimal inline form: collects field values, calls submit, shows the error
// or resets and calls onDone on success.
function InlineForm({
  fields,
  submitLabel,
  onSubmit,
  onDone,
}: {
  fields: Field[];
  submitLabel: string;
  onSubmit: (v: Record<string, string>) => Promise<ApiResult<unknown>>;
  onDone: () => void;
}) {
  const [v, setV] = useState<Record<string, string>>({});
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState("");

  async function submit(e: FormEvent) {
    e.preventDefault();
    setBusy(true);
    setErr("");
    const r = await onSubmit(v);
    setBusy(false);
    if (r.ok) {
      setV({});
      (e.target as HTMLFormElement).reset();
      onDone();
    } else {
      setErr(r.status === 402 ? "🔒 premium requis" : r.error);
    }
  }

  return (
    <form className="iform" onSubmit={submit}>
      {fields.map((f) =>
        f.options ? (
          <select
            key={f.name}
            required={f.required}
            value={v[f.name] ?? ""}
            onChange={(e) => setV({ ...v, [f.name]: e.target.value })}
          >
            <option value="">{f.label}</option>
            {f.options.map((o) => (
              <option key={o.value} value={o.value}>
                {o.label}
              </option>
            ))}
          </select>
        ) : (
          <input
            key={f.name}
            type={f.type ?? "text"}
            placeholder={f.label}
            required={f.required}
            value={v[f.name] ?? ""}
            onChange={(e) => setV({ ...v, [f.name]: e.target.value })}
          />
        ),
      )}
      <button disabled={busy} type="submit">
        {busy ? "…" : submitLabel}
      </button>
      {err && <span className="error small">{err}</span>}
    </form>
  );
}

function Section<T>({
  title,
  result,
  children,
}: {
  title: string;
  result: ApiResult<T> | null;
  children: (data: T) => React.ReactNode;
}) {
  return (
    <div className="card">
      <h2>{title}</h2>
      {result === null && <p className="muted">Chargement…</p>}
      {result && !result.ok && result.status === 402 && (
        <p className="muted">🔒 Fonctionnalité premium.</p>
      )}
      {result && !result.ok && result.status !== 402 && (
        <p className="error">{result.error}</p>
      )}
      {result && result.ok && children(result.data)}
    </div>
  );
}

type R<T> = ApiResult<T> | null;
type DataOf<F extends (...a: never[]) => unknown> =
  Awaited<ReturnType<F>> extends ApiResult<infer U> ? U : never;

function uuid() {
  return crypto.randomUUID();
}

function Dashboard({ onLogout }: { onLogout: () => void }) {
  const [me, setMe] = useState<R<DataOf<typeof api.me>>>(null);
  const [nw, setNw] = useState<R<DataOf<typeof api.networth>>>(null);
  const [goals, setGoals] = useState<R<DataOf<typeof api.goals>>>(null);
  const [budget, setBudget] = useState<R<DataOf<typeof api.getBudget>>>(null);
  const [trends, setTrends] = useState<R<DataOf<typeof api.trends>>>(null);
  const [forecast, setForecast] = useState<R<DataOf<typeof api.forecast>>>(null);

  const loadNw = useCallback(() => {
    api.networth().then((r) => {
      setNw(r);
      if (r.ok && r.data.accounts.length > 0) {
        const acc = r.data.accounts[0].id;
        api.trends(acc).then(setTrends);
        api.forecast(acc).then(setForecast);
      }
    });
  }, []);
  const loadGoals = useCallback(() => api.goals().then(setGoals), []);
  const loadBudget = useCallback(() => api.getBudget().then(setBudget), []);

  useEffect(() => {
    api.me().then(setMe);
    loadNw();
    loadGoals();
    loadBudget();
  }, [loadNw, loadGoals, loadBudget]);

  const accountOptions =
    nw && nw.ok
      ? nw.data.accounts.map((a) => ({ value: a.id, label: a.name }))
      : [];

  return (
    <div className="app">
      <header>
        <h1>WealthTorii</h1>
        <div>
          {me && me.ok && (
            <span className="muted">
              {me.data.email} · <b>{me.data.plan}</b>
            </span>
          )}
          <button className="link" onClick={onLogout}>
            Déconnexion
          </button>
        </div>
      </header>

      <div className="grid">
        <Section title="Patrimoine net" result={nw}>
          {(d) => (
            <>
              {d.totals.map((t) => (
                <div key={t.currency} className="big">
                  {t.net_worth.display}
                </div>
              ))}
              <table>
                <tbody>
                  {d.accounts.map((a) => (
                    <tr key={a.id}>
                      <td>{a.name}</td>
                      <td className="num">{a.balance.display}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
              <div className="addbox">
                <span className="muted small">Nouveau compte</span>
                <InlineForm
                  fields={[
                    { name: "id", label: "id (ex: bp-main)", required: true },
                    { name: "name", label: "nom", required: true },
                    { name: "opening_balance", label: "solde initial (ex: 1500,00)" },
                  ]}
                  submitLabel="Créer"
                  onSubmit={(v) =>
                    api.createAccount({
                      id: v.id,
                      name: v.name,
                      opening_balance: v.opening_balance || undefined,
                    })
                  }
                  onDone={loadNw}
                />
              </div>
            </>
          )}
        </Section>

        <Section title="Transactions" result={nw}>
          {() => (
            <div className="addbox">
              <span className="muted small">Nouvelle transaction</span>
              {accountOptions.length === 0 ? (
                <p className="muted small">Créez d'abord un compte.</p>
              ) : (
                <InlineForm
                  fields={[
                    {
                      name: "account_id",
                      label: "compte",
                      required: true,
                      options: accountOptions,
                    },
                    { name: "date", label: "date", type: "date", required: true },
                    {
                      name: "amount",
                      label: "montant (-12,90 = dépense)",
                      required: true,
                    },
                    { name: "description", label: "libellé" },
                    { name: "category_id", label: "catégorie (optionnel)" },
                  ]}
                  submitLabel="Ajouter"
                  onSubmit={(v) =>
                    api.createTransaction({
                      id: uuid(),
                      account_id: v.account_id,
                      date: v.date,
                      amount: v.amount,
                      description: v.description || undefined,
                      category_id: v.category_id || undefined,
                    })
                  }
                  onDone={loadNw}
                />
              )}
            </div>
          )}
        </Section>

        <Section title="Budget" result={budget}>
          {(d) => (
            <>
              {d.limits.length === 0 ? (
                <p className="muted">Aucune limite.</p>
              ) : (
                <table>
                  <tbody>
                    {d.limits.map((l) => (
                      <tr key={l.category}>
                        <td>{l.category}</td>
                        <td className="num">{l.limit.display}</td>
                      </tr>
                    ))}
                    <tr>
                      <td>
                        <b>Total</b>
                      </td>
                      <td className="num">
                        <b>{d.total.display}</b>
                      </td>
                    </tr>
                  </tbody>
                </table>
              )}
              <div className="addbox">
                <span className="muted small">Définir une limite</span>
                <InlineForm
                  fields={[
                    { name: "category", label: "catégorie", required: true },
                    { name: "amount", label: "montant (ex: 300,00)", required: true },
                  ]}
                  submitLabel="Enregistrer"
                  onSubmit={(v) =>
                    api.setBudget({ category: v.category, amount: v.amount })
                  }
                  onDone={loadBudget}
                />
              </div>
            </>
          )}
        </Section>

        <Section title="Objectifs d'épargne" result={goals}>
          {(d) => (
            <>
              {d.goals.map((g) => (
                <div key={g.id} className="goal">
                  <div className="goal-head">
                    <b>{g.name}</b>
                    <span>
                      {g.saved.display} / {g.target.display}
                    </span>
                  </div>
                  <div className="bar">
                    <div
                      className="bar-fill"
                      style={{ width: `${Math.min(100, g.progress_pct)}%` }}
                    />
                  </div>
                  <div className="muted small">
                    {g.progress_pct.toFixed(0)}%
                    {g.target_date ? ` · échéance ${g.target_date}` : ""}
                    {g.required_monthly
                      ? ` · ${g.required_monthly.display}/mois`
                      : ""}
                  </div>
                  <InlineForm
                    fields={[
                      { name: "amount", label: "contribuer (ex: 200,00)", required: true },
                      { name: "note", label: "note" },
                    ]}
                    submitLabel="+"
                    onSubmit={(v) =>
                      api.addContribution(g.id, {
                        amount: v.amount,
                        note: v.note || undefined,
                      })
                    }
                    onDone={() => {
                      loadGoals();
                      loadNw();
                    }}
                  />
                </div>
              ))}
              <div className="addbox">
                <span className="muted small">Nouvel objectif</span>
                <InlineForm
                  fields={[
                    { name: "name", label: "nom", required: true },
                    { name: "target", label: "cible (ex: 5000,00)", required: true },
                    { name: "target_date", label: "échéance", type: "date" },
                  ]}
                  submitLabel="Créer"
                  onSubmit={(v) =>
                    api.createGoal({
                      name: v.name,
                      target: v.target,
                      target_date: v.target_date || undefined,
                    })
                  }
                  onDone={loadGoals}
                />
              </div>
            </>
          )}
        </Section>

        <Section title="Tendances (6 mois)" result={trends}>
          {(d) =>
            d.months.length === 0 ? (
              <p className="muted">Pas de données.</p>
            ) : (
              <>
                <div className="legend">
                  <span>
                    <i className="sw-in" /> entrées
                  </span>
                  <span>
                    <i className="sw-out" /> sorties
                  </span>
                  <span className="muted">% = taux d'épargne</span>
                </div>
                <TrendsChart months={d.months} />
                <table>
                  <thead>
                    <tr>
                      <th>Mois</th>
                      <th className="num">Entrées</th>
                      <th className="num">Sorties</th>
                      <th className="num">Épargne</th>
                    </tr>
                  </thead>
                  <tbody>
                    {d.months.map((m) => (
                      <tr key={m.month}>
                        <td>{m.month}</td>
                        <td className="num">{m.inflow.display}</td>
                        <td className="num">{m.outflow.display}</td>
                        <td className="num">
                          {m.savings_rate_pct.toFixed(0)}%
                        </td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </>
            )
          }
        </Section>

        <Section title="Prévision de trésorerie" result={forecast}>
          {(d) => (
            <>
              <div className="muted small">
                {d.as_of} → {d.horizon}
              </div>
              <div className="row">
                <span>Solde actuel</span>
                <span className="num">{d.current_balance.display}</span>
              </div>
              <div className="row">
                <span>Solde projeté</span>
                <b className="num">{d.projected_balance.display}</b>
              </div>
              <div className="muted small">
                {d.expected.length} échéance(s) attendue(s)
              </div>
            </>
          )}
        </Section>
      </div>
    </div>
  );
}
