import { useEffect, useState, type FormEvent } from "react";
import { api, tokenStore, type ApiResult } from "./api";

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

// Renders the API result, showing a premium notice on 402.
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
// Success payload type of an api.* method.
type DataOf<F extends (...a: never[]) => unknown> =
  Awaited<ReturnType<F>> extends ApiResult<infer U> ? U : never;

function Dashboard({ onLogout }: { onLogout: () => void }) {
  const [me, setMe] = useState<R<DataOf<typeof api.me>>>(null);
  const [nw, setNw] = useState<R<DataOf<typeof api.networth>>>(null);
  const [goals, setGoals] = useState<R<DataOf<typeof api.goals>>>(null);
  const [trends, setTrends] = useState<R<DataOf<typeof api.trends>>>(null);
  const [forecast, setForecast] = useState<R<DataOf<typeof api.forecast>>>(null);

  useEffect(() => {
    api.me().then(setMe);
    api.goals().then(setGoals);
    api.networth().then((r) => {
      setNw(r);
      if (r.ok && r.data.accounts.length > 0) {
        const acc = r.data.accounts[0].id;
        api.trends(acc).then(setTrends);
        api.forecast(acc).then(setForecast);
      } else {
        setTrends({ ok: true, data: { account: "", months: [] } });
        setForecast(null);
      }
    });
  }, []);

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
              {d.totals.length === 0 && (
                <p className="muted">Aucun compte.</p>
              )}
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
            </>
          )}
        </Section>

        <Section title="Objectifs d'épargne" result={goals}>
          {(d) =>
            d.goals.length === 0 ? (
              <p className="muted">Aucun objectif.</p>
            ) : (
              d.goals.map((g) => (
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
                </div>
              ))
            )
          }
        </Section>

        <Section title="Tendances (6 mois)" result={trends}>
          {(d) =>
            d.months.length === 0 ? (
              <p className="muted">Pas de données.</p>
            ) : (
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
