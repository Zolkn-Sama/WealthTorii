# ADR-0002 : stockage = PostgreSQL conteneurisé via Docker

- Statut : Accepté (à valider à l'usage en Phase 2)
- Date : 2026-05-14

## Contexte

WealthTorii doit persister :
- des comptes (`ledger::Account`),
- des transactions (montants, dates, libellés, catégories),
- des budgets par catégorie et par période,
- à terme, des positions de portefeuille et des séries de prix.

L'outil cible un usage personnel d'abord, avec une API REST envisagée. Le reporting (totaux mensuels, répartition par catégorie, écart vs budget) suppose des requêtes agrégées par période/catégorie.

## Décision

Stockage en **PostgreSQL** exécuté dans un conteneur **Docker** (docker-compose pour l'environnement de dev).

- Type natif `NUMERIC(p, s)` côté SQL pour les montants — combiné à la stratégie `int64` minor_units côté C++ (cf. [ADR-0001](0001-money-int64-minor-units.md)), on peut soit stocker en `BIGINT` (minor_units bruts) soit en `NUMERIC(19, 4)`. Décision retenue : **`BIGINT` minor_units + colonne `currency CHAR(3)`** pour symétrie parfaite avec le modèle C++ et éviter tout aller-retour de conversion.
- Client C++ : **`libpqxx`** via vcpkg.
- Migrations : à choisir en Phase 2 (sqitch, dbmate ou maison via un dossier `migrations/`).

## Conséquences

- Le projet dépend d'un démon Docker pour le dev/test d'intégration. Les tests unitaires des libs `money`/`ledger` restent purs, sans DB.
- L'API REST aura une couche `infra/db` ou équivalent isolant la connexion ; le domaine (money, ledger, budget) ne dépend pas de Postgres.
- Un docker-compose minimal `infra/docker-compose.yml` sera ajouté en Phase 2 (Postgres + volume + variables d'env).
- Les tests d'intégration (`tests/integration/`) tournent contre une vraie DB éphémère (testcontainers C++ ou compose ad hoc).

## Alternatives écartées

- **SQLite** : envisagé initialement, écarté par l'utilisateur. Aurait évité Docker mais limite la concurrence et les types avancés (`NUMERIC`, fenêtres SQL solides).
- **Fichiers JSON/CSV plats** : reporting impraticable au-delà de quelques mois de données.
- **MySQL/MariaDB** : viable, mais Postgres offre un meilleur support des types décimaux, des CTE, des fenêtres et des contraintes — pertinent pour un domaine financier.
