# WealthTorii

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![CMake](https://img.shields.io/badge/CMake-3.28%2B-blue)
![vcpkg](https://img.shields.io/badge/deps-vcpkg-green)
![Tests](https://img.shields.io/badge/tests-GoogleTest-brightgreen)
![API](https://img.shields.io/badge/API-Drogon%20%2B%20Swagger-blue)
![Status](https://img.shields.io/badge/status-in%20progress-orange)

WealthTorii est une base de plateforme financière moderne en **C++** orientée :
- suivi de patrimoine,
- modélisation monétaire,
- logique de ledger,
- gestion de budget (règle 50/30/20, catégorisation),
- analytics financiers.

Le projet est pensé comme un socle sérieux, modulaire et testable, capable d’évoluer progressivement d’un usage personnel vers une application plus complète.

---

## Vision

WealthTorii a pour objectif de construire une fondation technique propre pour une application financière capable de gérer :

- des montants et devises de manière sûre,
- des comptes et transactions métier,
- l’import et la catégorisation de relevés bancaires,
- des budgets et des analyses de dépenses,
- une couche API et CLI,
- puis, à terme, une architecture plus ambitieuse.

Le projet privilégie d’abord :
- la **clarté du modèle métier**,
- la **qualité du code**,
- la **testabilité**,
- la **maintenabilité**.

---

## État actuel

Le projet a dépassé la phase de fondation : le modèle métier, la persistance
Postgres, une CLI complète et une API HTTP documentée par Swagger sont en place.

**Modules implémentés**
- `money` — montants/devises sûrs (entiers, unités mineures, cf. ADR-0001)
- `ledger` — comptes, transactions, journal
- `budget` — catégories, règle 50/30/20, comparaison budget/dépenses
- `import` — parsing CSV Banque Populaire + catégorisation par règles regex
- `storage` — persistance Postgres via `libpqxx` (migrations idempotentes)
- `analytics` — totaux mensuels, suggestions de budget

**Applications**
- `apps/cli` — binaire `wt` : `allocate`, `categories`, `import`, `report`,
  `budget`, `rules`, `sync`, `suggest`, `export`
- `apps/api` — binaire `wt_api` : serveur **Drogon** exposant toutes les
  fonctionnalités en HTTP, **Swagger UI** intégré et CRUD complet des
  comptes et transactions (OpenAPI 3.0)

**Modules prévus**
- `portfolio` — positions et performance
- `market_data` — données de marché

---

## Démarrage rapide

Prérequis : un toolchain C++20, CMake ≥ 3.28, [vcpkg](https://vcpkg.io)
(`VCPKG_ROOT` exporté), et Docker pour la base Postgres optionnelle.

```bash
# Configuration + build
cmake --preset dev
cmake --build build/dev

# CLI
./build/dev/apps/cli/wt help
./build/dev/apps/cli/wt allocate 1800
./build/dev/apps/cli/wt report DATA.csv --account bp-main

# Persistance Postgres (optionnelle, pour sync / --from-db / CRUD)
docker compose -f infra/docker-compose.yml up -d
export DATABASE_URL="postgresql://wealthtorii:wealthtorii@localhost:5544/wealthtorii"

# API + Swagger UI
DATABASE_URL="$DATABASE_URL" ./build/dev/apps/api/wt_api
# → http://127.0.0.1:8080/swagger
```

Tests : `ctest --preset test-dev` (les tests `storage` se sautent
automatiquement si `DATABASE_URL` est absent).

---

## API HTTP

`wt_api` sert un descriptif **OpenAPI 3.0** sur `/openapi.json` et une page
**Swagger UI** sur `/swagger` (la racine `/` y redirige).

| Domaine | Endpoints |
|---|---|
| Auth | `POST /api/auth/register`, `POST /api/auth/login`, `GET /api/auth/me` |
| Budget | `GET /api/allocate`, `GET/POST /api/budget`, `GET/DELETE /api/budget/{category}` |
| Catégories | `GET /api/categories` |
| Import | `POST /api/import`, `POST /api/report` |
| Règles | `GET/POST/PUT/DELETE /api/rules` |
| Comptes | `GET/POST /api/accounts`, `GET/PUT/DELETE /api/accounts/{id}`, `GET /api/accounts/{id}/balance` |
| Transactions | `GET/POST /api/transactions`, `GET/PUT/DELETE /api/transactions/{id}` |
| Patrimoine | `GET /api/networth` (soldes + totaux par devise) |
| Objectifs | `GET/POST /api/goals`, `GET/PUT/DELETE /api/goals/{id}`, `GET/POST /api/goals/{id}/contributions` |
| Storage | `POST /api/sync`, `GET /api/report` (depuis Postgres) |
| Analytics | `GET/POST /api/suggest`, `GET /api/trends` (mensuel + taux d'épargne) |
| Export | `GET/POST /api/export` (CSV format SORTED_DATA) |

### Authentification & freemium

L'inscription/connexion renvoie un **JWT** (Bearer). Tous les endpoints
`/api/*` (sauf `register`/`login`) exigent un token valide. Comptes,
transactions, **budgets et règles** sont **cloisonnés par utilisateur**
(`user_id`, en Postgres ; suppression d'un user → cascade).

| Tier | Fonctionnalités |
|---|---|
| **Gratuit** | `allocate`, `categories`, `budget`, `rules`, `import` |
| **Premium** | `report`, `suggest`, `export`, comptes, transactions, `sync`, `networth`, `trends`, `goals` |

Un utilisateur `free` qui appelle un endpoint premium reçoit `402 Payment
Required`. Le mot de passe est haché en **Argon2id** (libsodium) ; le JWT est
signé HS256 avec la variable d'environnement `JWT_SECRET` (valeur de dev par
défaut si absente). Le passage `free → premium` se fait pour l'instant côté
base (`UPDATE users SET plan='premium' …`) — pas encore d'intégration paiement.

> Note : le binaire CLI `wt` reste mono-utilisateur et conserve sa config
> fichier `~/.wealthtorii/*.conf` ; seule l'API est multi-utilisateur (Postgres).

Les endpoints adossés à Postgres répondent `500` si `DATABASE_URL` n’est pas
défini côté serveur.

---

## Stack technique

- **C++20**
- **CMake** + **CMake Presets**
- **vcpkg** pour la gestion des dépendances
- **Drogon** pour la couche HTTP
- **libsodium** (Argon2id) + **jwt-cpp** pour l'auth
- **libpqxx** + **PostgreSQL** pour la persistance
- **GoogleTest** pour les tests
- **clang-format** pour le formatage
- **clang-tidy** pour l’analyse statique
