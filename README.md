# WealthTorii

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)](https://img.shields.io/badge/C%2B%2B-20-blue) [![CMake](https://img.shields.io/badge/CMake-3.28%2B-blue)](https://img.shields.io/badge/CMake-3.28%2B-blue) [![vcpkg](https://img.shields.io/badge/deps-vcpkg-green)](https://img.shields.io/badge/deps-vcpkg-green) [![Tests](https://img.shields.io/badge/tests-GoogleTest-brightgreen)](https://img.shields.io/badge/tests-GoogleTest-brightgreen) [![API](https://img.shields.io/badge/API-Drogon%20%2B%20Swagger-blue)](https://img.shields.io/badge/API-Drogon%20%2B%20Swagger-blue) [![Status](https://img.shields.io/badge/status-in%20progress-orange)](https://img.shields.io/badge/status-in%20progress-orange) [![Vibecoded](https://img.shields.io/badge/built%20with-vibecoding-%23ff69b4)](https://img.shields.io/badge/built%20with-vibecoding-%23ff69b4)

WealthTorii est une base de plateforme financière moderne en **C++** orientée :

- suivi de patrimoine,
- modélisation monétaire,
- logique de ledger,
- gestion de budget (règle 50/30/20, catégorisation),
- analytics financiers.

Le projet est pensé comme un socle sérieux, modulaire et testable, capable d'évoluer progressivement d'un usage personnel vers une application plus complète. **Le projet est entièrement gratuit, sans fonctionnalité payante ni intention de monétisation.**

---

## Vibecodé, avec exigence

[#vibecodé-avec-exigence](#vibecodé-avec-exigence)

WealthTorii est développé en **vibecoding** : une grande partie du code, de l'architecture et de la documentation est produite en collaboration étroite avec des assistants IA, sous supervision humaine constante. Ce choix de méthode ne dispense d'aucune exigence de qualité — il vient au contraire avec des garde-fous plus stricts, précisément parce que la vitesse de génération de code impose une discipline de vérification renforcée :

- **Rien ne rentre sans passer par les tests.** Chaque module métier (`money`, `ledger`, `budget`, `analytics`, `portfolio`...) est couvert par GoogleTest ; `ctest` fait partie du cycle normal, pas d'une étape optionnelle.
- **Analyse statique systématique.** `clang-tidy` et `clang-format` sont appliqués en continu pour éviter la dérive de style et attraper les erreurs classiques (fuites, UB, conversions implicites dangereuses) que du code généré rapidement peut introduire.
- **Décisions d'architecture tracées.** Les choix structurants (ex. représentation des montants en entiers/unités mineures, cf. ADR-0001) sont documentés dans `docs/`, relus, et non simplement acceptés parce qu'une IA les a proposés.
- **Revue humaine avant fusion.** Le code généré est lu, compris et challengé avant d'être intégré ; l'objectif n'est pas de produire du volume mais un socle que l'on peut maintenir et faire évoluer en confiance.
- **Transparence de la méthode.** Ce README assume la méthode plutôt que de la cacher : c'est un choix d'outillage, pas un raccourci sur la rigueur.

L'idée sous-jacente : le vibecoding accélère l'exploration et l'écriture, mais la qualité, elle, reste entièrement une responsabilité humaine — tests, revue, documentation et gestion de projet ne sont jamais déléguées.

---

## Vision

[#vision](#vision)

WealthTorii a pour objectif de construire une fondation technique propre pour une application financière capable de gérer :

- des montants et devises de manière sûre,
- des comptes et transactions métier,
- l'import et la catégorisation de relevés bancaires,
- des budgets et des analyses de dépenses,
- une couche API et CLI,
- puis, à terme, une architecture plus ambitieuse.

Le projet privilégie d'abord :

- la **clarté du modèle métier**,
- la **qualité du code**,
- la **testabilité**,
- la **maintenabilité**.

---

## État actuel & Roadmap

[#état-actuel--roadmap](#état-actuel--roadmap)

Le projet a dépassé la phase de fondation : le modèle métier, la persistance
Postgres, une CLI complète et une API HTTP documentée par Swagger sont en place.

### Modules implémentés

| Module | Rôle |
| --- | --- |
| `money` | montants/devises sûrs (entiers, unités mineures, cf. ADR-0001) |
| `ledger` | comptes, transactions, journal |
| `budget` | catégories, règle 50/30/20, comparaison budget/dépenses |
| `import` | parsing CSV Banque Populaire + catégorisation par règles regex |
| `storage` | persistance Postgres via `libpqxx` (migrations idempotentes) |
| `analytics` | totaux mensuels, suggestions de budget, détection des récurrents |
| `portfolio` | valorisation de positions (coût moyen), +/-value latente |
| `market_data` | récupération de cours via Stooq (CSV, sans clé) |

### Applications

- `apps/cli` — binaire `wt` : `allocate`, `categories`, `import`, `report`, `budget`, `rules`, `sync`, `suggest`, `export`
- `apps/api` — binaire `wt_api` : serveur **Drogon** exposant toutes les fonctionnalités en HTTP, **Swagger UI** intégré et CRUD complet des comptes et transactions (OpenAPI 3.0)

### En cours / prochaines étapes

- [ ] `market_data` : rafraîchissement périodique automatique
- [ ] `market_data` : conversion FX (les cours Stooq sont aujourd'hui stockés tels quels dans la devise de la position)
- [ ] Renforcement de la couverture de tests sur les modules `analytics` et `portfolio`
- [ ] Documentation utilisateur de l'interface web (`web/`)
- [ ] Consolidation des ADR au fur et à mesure des décisions structurantes

### Suivi de projet

- Les décisions d'architecture sont consignées dans `docs/` sous forme d'ADR (Architecture Decision Records), numérotées et datées.
- L'avancement fonctionnel est reflété directement dans cette section : un module n'apparaît dans "Modules implémentés" que lorsqu'il est testé et documenté, pas dès la première ébauche.
- Les évolutions prévues sont listées ci-dessus sous forme de checklist plutôt que dans un backlog externe, pour que l'état du projet reste lisible directement depuis le README.

---

## Démarrage rapide

[#démarrage-rapide](#démarrage-rapide)

Prérequis : un toolchain C++20, CMake ≥ 3.28, [vcpkg](https://vcpkg.io) (`VCPKG_ROOT` exporté), et Docker pour la base Postgres optionnelle.

```
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

# Interface web (React) — buildée puis servie par l'API
npm --prefix web ci
npm --prefix web run build          # génère web/dist (servi en statique)

# API + UI web + Swagger UI
DATABASE_URL="$DATABASE_URL" ./build/dev/apps/api/wt_api
# → http://127.0.0.1:8080         (UI web si web/dist présent)
# → http://127.0.0.1:8080/swagger (doc API)
```

Tests : `ctest --preset test-dev` (les tests `storage` se sautent
automatiquement si `DATABASE_URL` est absent).
> Sans `web/dist`, `/` redirige vers `/swagger` — l'UI est optionnelle,
> l'API reste pleinement fonctionnelle. Dev front : `npm --prefix web run dev` (proxie `/api` vers `:8080`).

---

## API HTTP

[#api-http](#api-http)

`wt_api` sert un descriptif **OpenAPI 3.0** sur `/openapi.json` et une page **Swagger UI** sur `/swagger` (la racine `/` y redirige).

| Domaine         | Endpoints                                                                                                                                                                                                |
| --------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Auth            | `POST /api/auth/register`, `POST /api/auth/login`, `GET /api/auth/me`                                                                                                                                    |
| Budget          | `GET /api/allocate`, `GET/POST /api/budget`, `GET/DELETE /api/budget/{category}`                                                                                                                         |
| Catégories      | `GET /api/categories`                                                                                                                                                                                    |
| Import          | `POST /api/import`, `POST /api/report`                                                                                                                                                                   |
| Règles          | `GET/POST/PUT/DELETE /api/rules`                                                                                                                                                                         |
| Comptes         | `GET/POST /api/accounts`, `GET/PUT/DELETE /api/accounts/{id}`, `GET /api/accounts/{id}/balance`                                                                                                          |
| Transactions    | `GET/POST /api/transactions`, `GET/PUT/DELETE /api/transactions/{id}`                                                                                                                                    |
| Patrimoine      | `GET /api/networth` (soldes + investissements + totaux par devise)                                                                                                                                       |
| Objectifs       | `GET/POST /api/goals`, `GET/PUT/DELETE /api/goals/{id}`, `GET/POST /api/goals/{id}/contributions`                                                                                                        |
| Investissements | `GET /api/portfolio`, `GET/POST /api/positions`, `PUT/DELETE /api/positions/{id}`, `GET /api/prices`, `PUT/DELETE /api/prices/{symbol}`, `POST /api/prices/refresh` (Stooq)                              |
| Storage         | `POST /api/sync`, `GET /api/report` (depuis Postgres)                                                                                                                                                    |
| Analytics       | `GET/POST /api/suggest`, `GET /api/trends` (mensuel + taux d'épargne), `GET /api/recurring` (récurrents détectés), `GET /api/forecast` (solde projeté), `GET /api/plan` (allocation perso + reste à vivre) |
| Export          | `GET/POST /api/export` (CSV format SORTED\_DATA)                                                                                                                                                         |

### Authentification

[#authentification](#authentification)

L'inscription/connexion renvoie un **JWT** (Bearer). Tous les endpoints `/api/*` (sauf `register`/`login`) exigent un token valide. Comptes,
transactions, budgets et règles sont **cloisonnés par utilisateur** (`user_id`, en Postgres ; suppression d'un user → cascade).

**Toutes les fonctionnalités sont accessibles à tous les utilisateurs, sans distinction de palier.** Il n'y a pas de fonctionnalités premium, pas de restriction d'usage, et aucune intention d'introduire un modèle payant : WealthTorii est un projet gratuit, pensé comme un socle technique ouvert.

Le mot de passe est haché en **Argon2id** (libsodium) ; le JWT est
signé HS256 avec la variable d'environnement `JWT_SECRET` (valeur de dev par
défaut si absente).
> Note : le binaire CLI `wt` reste mono-utilisateur et conserve sa config
> fichier `~/.wealthtorii/*.conf` ; seule l'API est multi-utilisateur (Postgres).

Les endpoints adossés à Postgres répondent `500` si `DATABASE_URL` n'est pas
défini côté serveur.

---

## Stack technique

[#stack-technique](#stack-technique)

- **C++20**
- **CMake** + **CMake Presets**
- **vcpkg** pour la gestion des dépendances
- **Drogon** pour la couche HTTP (sert aussi l'UI en statique)
- **React + Vite + TypeScript** pour l'interface web (`web/`)
- **libsodium** (Argon2id) + **jwt-cpp** pour l'auth
- **libpqxx** + **PostgreSQL** pour la persistance
- **GoogleTest** pour les tests
- **clang-format** pour le formatage
- **clang-tidy** pour l'analyse statique
