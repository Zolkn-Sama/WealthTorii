#pragma once

namespace wealthtorii::api {

    // Self-contained Swagger UI page (assets from the jsDelivr CDN) pointing at /openapi.json.
    inline constexpr const char* kSwaggerHtml = R"HTML(<!DOCTYPE html>
<html lang="fr">
<head>
  <meta charset="utf-8"/>
  <title>WealthTorii API — Swagger</title>
  <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/swagger-ui-dist@5/swagger-ui.css"/>
  <style>body{margin:0}</style>
</head>
<body>
  <div id="swagger-ui"></div>
  <script src="https://cdn.jsdelivr.net/npm/swagger-ui-dist@5/swagger-ui-bundle.js"></script>
  <script>
    window.onload = () => {
      window.ui = SwaggerUIBundle({
        url: '/openapi.json',
        dom_id: '#swagger-ui',
        deepLinking: true,
        presets: [SwaggerUIBundle.presets.apis],
      });
    };
  </script>
</body>
</html>)HTML";

    // OpenAPI 3.0 description of every implemented endpoint.
    inline constexpr const char* kOpenApiJson = R"JSON({
  "openapi": "3.0.3",
  "info": {
    "title": "WealthTorii API",
    "version": "0.1.0",
    "description": "Couche HTTP exposant les fonctionnalites deja implementees du CLI WealthTorii (allocate, categories, import, report, budget, rules, sync, suggest, export). Les endpoints --from-db necessitent la variable d'environnement DATABASE_URL cote serveur."
  },
  "servers": [{ "url": "/" }],
  "tags": [
    { "name": "budget", "description": "Allocation 50/30/20 et configuration des limites" },
    { "name": "categories", "description": "Registre de categories" },
    { "name": "accounts", "description": "CRUD des comptes (Postgres)" },
    { "name": "transactions", "description": "CRUD des transactions (Postgres)" },
    { "name": "import", "description": "Import / analyse de CSV Banque Populaire" },
    { "name": "rules", "description": "Regles de categorisation utilisateur" },
    { "name": "storage", "description": "Persistance Postgres" },
    { "name": "analytics", "description": "Suggestions de budget" },
    { "name": "export", "description": "Export CSV" }
  ],
  "paths": {
    "/api/allocate": {
      "get": {
        "tags": ["budget"],
        "summary": "Repartition 50/30/20 d'un revenu",
        "parameters": [
          { "name": "income", "in": "query", "required": true, "schema": { "type": "string" }, "example": "1800", "description": "Montant FR (1 234,56) ou EN (1234.56)" },
          { "name": "currency", "in": "query", "required": false, "schema": { "type": "string", "enum": ["EUR","USD","CHF"], "default": "EUR" } }
        ],
        "responses": {
          "200": { "description": "Repartition par bucket", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Allocation" } } } },
          "400": { "$ref": "#/components/responses/BadRequest" }
        }
      }
    },
    "/api/categories": {
      "get": {
        "tags": ["categories"],
        "summary": "Registre de categories par defaut",
        "responses": {
          "200": { "description": "Categories groupees par bucket", "content": { "application/json": { "schema": { "type": "object" } } } }
        }
      }
    },
    "/api/import": {
      "post": {
        "tags": ["import"],
        "summary": "Parse un CSV Banque Populaire et renvoie un resume",
        "requestBody": { "required": true, "content": { "multipart/form-data": { "schema": {
          "type": "object",
          "required": ["file","account"],
          "properties": {
            "file": { "type": "string", "format": "binary", "description": "Export CSV Banque Populaire" },
            "account": { "type": "string", "description": "Identifiant de compte" }
          }
        } } } },
        "responses": {
          "200": { "description": "Resume d'import", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/ImportReport" } } } },
          "400": { "$ref": "#/components/responses/BadRequest" }
        }
      }
    },
    "/api/report": {
      "post": {
        "tags": ["import"],
        "summary": "Rapport mensuel a partir d'un CSV uploade",
        "requestBody": { "required": true, "content": { "multipart/form-data": { "schema": {
          "type": "object",
          "required": ["file","account"],
          "properties": {
            "file": { "type": "string", "format": "binary" },
            "account": { "type": "string" },
            "month": { "type": "string", "example": "2026-03", "description": "YYYY-MM (defaut: dernier mois present)" }
          }
        } } } },
        "responses": {
          "200": { "description": "Rapport mensuel", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Report" } } } },
          "400": { "$ref": "#/components/responses/BadRequest" }
        }
      },
      "get": {
        "tags": ["storage"],
        "summary": "Rapport mensuel depuis Postgres",
        "parameters": [
          { "name": "account", "in": "query", "required": true, "schema": { "type": "string" } },
          { "name": "month", "in": "query", "required": false, "schema": { "type": "string" }, "example": "2026-03" }
        ],
        "responses": {
          "200": { "description": "Rapport mensuel", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Report" } } } },
          "400": { "$ref": "#/components/responses/BadRequest" },
          "500": { "$ref": "#/components/responses/ServerError" }
        }
      }
    },
    "/api/budget": {
      "get": {
        "tags": ["budget"],
        "summary": "Configuration de budget sauvegardee",
        "responses": { "200": { "description": "Limites par categorie", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/BudgetConfig" } } } } }
      },
      "post": {
        "tags": ["budget"],
        "summary": "Definit une limite mensuelle pour une categorie",
        "requestBody": { "required": true, "content": { "application/json": { "schema": {
          "type": "object",
          "required": ["category","amount"],
          "properties": {
            "category": { "type": "string", "example": "groceries" },
            "amount": { "type": "string", "example": "300,00" },
            "currency": { "type": "string", "enum": ["EUR","USD","CHF"] }
          }
        } } } },
        "responses": {
          "200": { "description": "Configuration mise a jour", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/BudgetConfig" } } } },
          "400": { "$ref": "#/components/responses/BadRequest" }
        }
      }
    },
    "/api/budget/{category}": {
      "get": {
        "tags": ["budget"],
        "summary": "Limite d'une categorie (getById)",
        "parameters": [ { "name": "category", "in": "path", "required": true, "schema": { "type": "string" }, "example": "groceries" } ],
        "responses": {
          "200": { "description": "Limite", "content": { "application/json": { "schema": { "type": "object" } } } },
          "404": { "$ref": "#/components/responses/BadRequest" }
        }
      },
      "delete": {
        "tags": ["budget"],
        "summary": "Supprime la limite d'une categorie",
        "parameters": [ { "name": "category", "in": "path", "required": true, "schema": { "type": "string" } } ],
        "responses": {
          "200": { "description": "Configuration mise a jour", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/BudgetConfig" } } } },
          "404": { "$ref": "#/components/responses/BadRequest" }
        }
      }
    },
    "/api/rules": {
      "get": {
        "tags": ["rules"],
        "summary": "Liste des regles, ou une seule via ?pattern= (getByString)",
        "parameters": [ { "name": "pattern", "in": "query", "required": false, "schema": { "type": "string" }, "description": "Si fourni, renvoie uniquement la regle a ce pattern exact" } ],
        "responses": {
          "200": { "description": "Regles", "content": { "application/json": { "schema": { "type": "object" } } } },
          "404": { "$ref": "#/components/responses/BadRequest" }
        }
      },
      "post": {
        "tags": ["rules"],
        "summary": "Cree ou met a jour une regle regex (upsert)",
        "requestBody": { "required": true, "content": { "application/json": { "schema": {
          "type": "object",
          "required": ["pattern","category"],
          "properties": {
            "pattern": { "type": "string", "example": "NETFLIX" },
            "category": { "type": "string", "example": "subscriptions-leisure" },
            "bp_subcategory": { "type": "string", "nullable": true }
          }
        } } } },
        "responses": {
          "200": { "description": "Regle ajoutee/maj", "content": { "application/json": { "schema": { "type": "object" } } } },
          "400": { "$ref": "#/components/responses/BadRequest" }
        }
      },
      "put": {
        "tags": ["rules"],
        "summary": "Met a jour une regle existante (404 si absente)",
        "requestBody": { "required": true, "content": { "application/json": { "schema": {
          "type": "object",
          "required": ["pattern","category"],
          "properties": {
            "pattern": { "type": "string", "example": "NETFLIX" },
            "category": { "type": "string", "example": "subscriptions-leisure" },
            "bp_subcategory": { "type": "string", "nullable": true }
          }
        } } } },
        "responses": {
          "200": { "description": "Regle mise a jour", "content": { "application/json": { "schema": { "type": "object" } } } },
          "404": { "$ref": "#/components/responses/BadRequest" }
        }
      },
      "delete": {
        "tags": ["rules"],
        "summary": "Supprime une regle par pattern exact",
        "parameters": [ { "name": "pattern", "in": "query", "required": true, "schema": { "type": "string" } } ],
        "responses": {
          "200": { "description": "Regle supprimee", "content": { "application/json": { "schema": { "type": "object" } } } },
          "404": { "$ref": "#/components/responses/BadRequest" }
        }
      }
    },
    "/api/accounts": {
      "get": {
        "tags": ["accounts"],
        "summary": "Liste tous les comptes (getAll)",
        "responses": {
          "200": { "description": "Comptes", "content": { "application/json": { "schema": { "type": "object" } } } },
          "500": { "$ref": "#/components/responses/ServerError" }
        }
      },
      "post": {
        "tags": ["accounts"],
        "summary": "Cree un compte (409 si l'id existe deja)",
        "requestBody": { "required": true, "content": { "application/json": { "schema": { "$ref": "#/components/schemas/AccountInput" } } } },
        "responses": {
          "201": { "description": "Compte cree", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Account" } } } },
          "400": { "$ref": "#/components/responses/BadRequest" },
          "409": { "$ref": "#/components/responses/BadRequest" },
          "500": { "$ref": "#/components/responses/ServerError" }
        }
      }
    },
    "/api/accounts/{id}": {
      "get": {
        "tags": ["accounts"],
        "summary": "Recupere un compte par id (getById)",
        "parameters": [ { "name": "id", "in": "path", "required": true, "schema": { "type": "string" }, "example": "bp-main" } ],
        "responses": {
          "200": { "description": "Compte", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Account" } } } },
          "404": { "$ref": "#/components/responses/BadRequest" },
          "500": { "$ref": "#/components/responses/ServerError" }
        }
      },
      "put": {
        "tags": ["accounts"],
        "summary": "Met a jour un compte existant (404 si absent)",
        "parameters": [ { "name": "id", "in": "path", "required": true, "schema": { "type": "string" } } ],
        "requestBody": { "required": true, "content": { "application/json": { "schema": { "$ref": "#/components/schemas/AccountInput" } } } },
        "responses": {
          "200": { "description": "Compte mis a jour", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Account" } } } },
          "404": { "$ref": "#/components/responses/BadRequest" },
          "500": { "$ref": "#/components/responses/ServerError" }
        }
      },
      "delete": {
        "tags": ["accounts"],
        "summary": "Supprime un compte (cascade sur ses transactions)",
        "parameters": [ { "name": "id", "in": "path", "required": true, "schema": { "type": "string" } } ],
        "responses": {
          "200": { "description": "Compte supprime", "content": { "application/json": { "schema": { "type": "object" } } } },
          "404": { "$ref": "#/components/responses/BadRequest" },
          "500": { "$ref": "#/components/responses/ServerError" }
        }
      }
    },
    "/api/transactions": {
      "get": {
        "tags": ["transactions"],
        "summary": "Transactions d'un compte (getAll), filtre optionnel par mois",
        "parameters": [
          { "name": "account", "in": "query", "required": true, "schema": { "type": "string" }, "example": "bp-main" },
          { "name": "month", "in": "query", "required": false, "schema": { "type": "string" }, "example": "2026-03" }
        ],
        "responses": {
          "200": { "description": "Transactions", "content": { "application/json": { "schema": { "type": "object" } } } },
          "400": { "$ref": "#/components/responses/BadRequest" },
          "500": { "$ref": "#/components/responses/ServerError" }
        }
      },
      "post": {
        "tags": ["transactions"],
        "summary": "Cree une transaction (409 si l'id existe deja)",
        "requestBody": { "required": true, "content": { "application/json": { "schema": { "$ref": "#/components/schemas/TransactionInput" } } } },
        "responses": {
          "201": { "description": "Transaction creee", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Transaction" } } } },
          "400": { "$ref": "#/components/responses/BadRequest" },
          "409": { "$ref": "#/components/responses/BadRequest" },
          "500": { "$ref": "#/components/responses/ServerError" }
        }
      }
    },
    "/api/transactions/{id}": {
      "get": {
        "tags": ["transactions"],
        "summary": "Recupere une transaction par id (getById)",
        "parameters": [ { "name": "id", "in": "path", "required": true, "schema": { "type": "string" } } ],
        "responses": {
          "200": { "description": "Transaction", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Transaction" } } } },
          "404": { "$ref": "#/components/responses/BadRequest" },
          "500": { "$ref": "#/components/responses/ServerError" }
        }
      },
      "put": {
        "tags": ["transactions"],
        "summary": "Met a jour une transaction existante (404 si absente)",
        "parameters": [ { "name": "id", "in": "path", "required": true, "schema": { "type": "string" } } ],
        "requestBody": { "required": true, "content": { "application/json": { "schema": { "$ref": "#/components/schemas/TransactionInput" } } } },
        "responses": {
          "200": { "description": "Transaction mise a jour", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Transaction" } } } },
          "404": { "$ref": "#/components/responses/BadRequest" },
          "500": { "$ref": "#/components/responses/ServerError" }
        }
      },
      "delete": {
        "tags": ["transactions"],
        "summary": "Supprime une transaction",
        "parameters": [ { "name": "id", "in": "path", "required": true, "schema": { "type": "string" } } ],
        "responses": {
          "200": { "description": "Transaction supprimee", "content": { "application/json": { "schema": { "type": "object" } } } },
          "404": { "$ref": "#/components/responses/BadRequest" },
          "500": { "$ref": "#/components/responses/ServerError" }
        }
      }
    },
    "/api/sync": {
      "post": {
        "tags": ["storage"],
        "summary": "Importe un CSV et persiste dans Postgres",
        "requestBody": { "required": true, "content": { "multipart/form-data": { "schema": {
          "type": "object",
          "required": ["file","account"],
          "properties": {
            "file": { "type": "string", "format": "binary" },
            "account": { "type": "string" }
          }
        } } } },
        "responses": {
          "200": { "description": "Statistiques d'upsert", "content": { "application/json": { "schema": { "type": "object" } } } },
          "400": { "$ref": "#/components/responses/BadRequest" },
          "500": { "$ref": "#/components/responses/ServerError" }
        }
      }
    },
    "/api/suggest": {
      "post": {
        "tags": ["analytics"],
        "summary": "Suggere des budgets a partir d'un CSV uploade",
        "requestBody": { "required": true, "content": { "multipart/form-data": { "schema": {
          "type": "object",
          "required": ["file","account"],
          "properties": {
            "file": { "type": "string", "format": "binary" },
            "account": { "type": "string" },
            "months": { "type": "integer", "default": 3 },
            "ending": { "type": "string", "example": "2026-03" }
          }
        } } } },
        "responses": {
          "200": { "description": "Suggestions", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Suggestions" } } } },
          "400": { "$ref": "#/components/responses/BadRequest" }
        }
      },
      "get": {
        "tags": ["analytics"],
        "summary": "Suggere des budgets depuis Postgres",
        "parameters": [
          { "name": "account", "in": "query", "required": true, "schema": { "type": "string" } },
          { "name": "months", "in": "query", "required": false, "schema": { "type": "integer", "default": 3 } },
          { "name": "ending", "in": "query", "required": false, "schema": { "type": "string" }, "example": "2026-03" }
        ],
        "responses": {
          "200": { "description": "Suggestions", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Suggestions" } } } },
          "400": { "$ref": "#/components/responses/BadRequest" },
          "500": { "$ref": "#/components/responses/ServerError" }
        }
      }
    },
    "/api/export": {
      "post": {
        "tags": ["export"],
        "summary": "Exporte un CSV uploade au format SORTED_DATA (11 colonnes)",
        "requestBody": { "required": true, "content": { "multipart/form-data": { "schema": {
          "type": "object",
          "required": ["file","account"],
          "properties": {
            "file": { "type": "string", "format": "binary" },
            "account": { "type": "string" }
          }
        } } } },
        "responses": {
          "200": { "description": "CSV exporte", "content": { "text/csv": { "schema": { "type": "string" } } } },
          "400": { "$ref": "#/components/responses/BadRequest" }
        }
      },
      "get": {
        "tags": ["export"],
        "summary": "Exporte les transactions Postgres au format SORTED_DATA",
        "parameters": [ { "name": "account", "in": "query", "required": true, "schema": { "type": "string" } } ],
        "responses": {
          "200": { "description": "CSV exporte", "content": { "text/csv": { "schema": { "type": "string" } } } },
          "400": { "$ref": "#/components/responses/BadRequest" },
          "500": { "$ref": "#/components/responses/ServerError" }
        }
      }
    }
  },
  "components": {
    "responses": {
      "BadRequest": { "description": "Requete invalide", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Error" } } } },
      "ServerError": { "description": "Erreur serveur (ex: DATABASE_URL absent)", "content": { "application/json": { "schema": { "$ref": "#/components/schemas/Error" } } } }
    },
    "schemas": {
      "Error": { "type": "object", "properties": { "error": { "type": "string" } } },
      "Money": {
        "type": "object",
        "properties": {
          "minor_units": { "type": "integer", "format": "int64" },
          "currency": { "type": "string" },
          "display": { "type": "string" }
        }
      },
      "Allocation": {
        "type": "object",
        "properties": {
          "income": { "$ref": "#/components/schemas/Money" },
          "needs": { "$ref": "#/components/schemas/Money" },
          "wants": { "$ref": "#/components/schemas/Money" },
          "savings_invest": { "$ref": "#/components/schemas/Money" }
        }
      },
      "Account": {
        "type": "object",
        "properties": {
          "id": { "type": "string" },
          "name": { "type": "string" },
          "currency": { "type": "string", "enum": ["EUR","USD","CHF"] },
          "type": { "type": "string", "enum": ["CASH","BROKERAGE","CRYPTO","SAVINGS","EXTERNAL"] },
          "is_active": { "type": "boolean" }
        }
      },
      "AccountInput": {
        "type": "object",
        "required": ["id","name"],
        "properties": {
          "id": { "type": "string", "example": "bp-main" },
          "name": { "type": "string", "example": "Compte courant BP" },
          "currency": { "type": "string", "enum": ["EUR","USD","CHF"], "default": "EUR" },
          "type": { "type": "string", "enum": ["CASH","BROKERAGE","CRYPTO","SAVINGS","EXTERNAL"], "default": "CASH" },
          "is_active": { "type": "boolean", "default": true }
        }
      },
      "Transaction": {
        "type": "object",
        "properties": {
          "id": { "type": "string" },
          "account_id": { "type": "string" },
          "date": { "type": "string", "example": "2026-03-14" },
          "amount": { "$ref": "#/components/schemas/Money" },
          "description": { "type": "string" },
          "category_id": { "type": "string", "nullable": true },
          "bp_category": { "type": "string", "nullable": true },
          "bp_subcategory": { "type": "string", "nullable": true },
          "type_operation": { "type": "string" },
          "is_reconciled": { "type": "boolean" }
        }
      },
      "TransactionInput": {
        "type": "object",
        "required": ["id","account_id","date"],
        "properties": {
          "id": { "type": "string", "example": "tx-0001" },
          "account_id": { "type": "string", "example": "bp-main" },
          "date": { "type": "string", "example": "2026-03-14", "description": "YYYY-MM-DD" },
          "amount": { "type": "string", "example": "-42,90 EUR", "description": "Signe; FR/EN. Alternative: minor_units + currency" },
          "minor_units": { "type": "integer", "format": "int64", "description": "Si 'amount' absent" },
          "currency": { "type": "string", "enum": ["EUR","USD","CHF"], "default": "EUR" },
          "description": { "type": "string" },
          "category_id": { "type": "string", "nullable": true },
          "bp_category": { "type": "string", "nullable": true },
          "bp_subcategory": { "type": "string", "nullable": true },
          "type_operation": { "type": "string" },
          "is_reconciled": { "type": "boolean", "default": false }
        }
      },
      "ImportReport": {
        "type": "object",
        "properties": {
          "rows_seen": { "type": "integer" },
          "rows_dropped": { "type": "integer" },
          "transactions": { "type": "integer" },
          "categorised": { "type": "integer" },
          "uncategorised": { "type": "integer" }
        }
      },
      "Report": {
        "type": "object",
        "properties": {
          "account": { "type": "string" },
          "month": { "type": "string" },
          "inflow": { "$ref": "#/components/schemas/Money" },
          "outflow": { "$ref": "#/components/schemas/Money" },
          "net": { "$ref": "#/components/schemas/Money" },
          "by_bucket": { "type": "array", "items": { "type": "object" } },
          "by_category": { "type": "array", "items": { "type": "object" } },
          "budget_vs_spending": { "type": "array", "items": { "type": "object" } }
        }
      },
      "BudgetConfig": {
        "type": "object",
        "properties": {
          "currency": { "type": "string" },
          "limits": { "type": "array", "items": { "type": "object" } },
          "total": { "$ref": "#/components/schemas/Money" }
        }
      },
      "Suggestions": {
        "type": "object",
        "properties": {
          "months": { "type": "integer" },
          "ending": { "type": "string" },
          "suggestions": { "type": "array", "items": { "type": "object" } }
        }
      }
    }
  }
})JSON";

} // namespace wealthtorii::api
